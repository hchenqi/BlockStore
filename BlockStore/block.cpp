#include "block_manager.h"
#include "CppSerialize/cpp_serialize.h"
#include "SQLite3Helper/sqlite3_helper.h"

#include <memory>
#include <unordered_set>
#include <vector>


BEGIN_NAMESPACE(Anonymous)

using namespace SQLite3Helper;
using namespace CppSerialize;


class DB : public Database {
private:
	constexpr static uint64 schema_version = 2025'09'25'1;

	enum GcPhase : unsigned char { Idle, Scanning, Sweeping };

	struct Metadata {
		uint64 version;
		index_t root_index;
		bool gc_mark;
		GcPhase gc_phase;
		uint64 block_count_last_gc;
		uint64 block_count;
		uint64 block_count_marked;
		index_t max_index;
		index_t gc_delete_index;
	};

	constexpr static uint64 table_count = 3;

	Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> count: uint64

	Query create_STATIC = "create table STATIC (data BLOB)";  // void -> void
	Query create_OBJECT = "create table OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB, ref BLOB)";  // void -> void
	Query create_SCAN = "create table SCAN (id INTEGER)";  // void -> void

	Query insert_STATIC_data = "insert into STATIC values (?)";  // data: vector<byte> -> void
	Query select_data_STATIC = "select * from STATIC";  // void -> data: vector<byte>
	Query update_STATIC_data = "update STATIC set data = ?";  // data: vector<byte> -> void

	Query insert_id_OBJECT_gc = "insert into OBJECT (gc) values (?) returning id";  // gc: bool -> id: index_t

	Query select_data_OBJECT_id = "select data from OBJECT where id = ?";  // id: index_t -> data: vector<byte>
	Query update_OBJECT_data_ref_id = "update OBJECT set data = ?, ref = ? where id = ?";  // data: vector<byte>, ref: vector<index_t>, id: index_t -> void
	Query update_gc_OBJECT_data_ref_id = "update OBJECT set data = ?, ref = ? where id = ? returning gc";  // data: vector<byte>, ref: vector<index_t>, id: index_t -> gc: bool

	Query select_count_SCAN = "select count(*) from SCAN";  // void -> count: uint64
	Query update_ref_OBJECT_gc = "update OBJECT set gc = ? where id in (select id from SCAN order by rowid desc limit ?) and gc = ? returning ref";  // gc: bool, limit: uint64, gc: bool -> vector<ref: vector<index_t>>
	Query delete_SCAN_limit = "delete from SCAN where rowid in (select rowid from SCAN order by rowid desc limit ?)";  // limit: uint64 -> void
	Query insert_SCAN_id = "insert into SCAN values (?)";  // id: index_t -> void

	Query select_count_OBJECT = "select count(*) from OBJECT";  // void -> count: uint64
	Query select_max_OBJECT = "select max(id) from OBJECT";  // void -> id: index_t
	Query select_next_OBJECT_begin_limit = "select max(id) from OBJECT where id >= ? order by id asc limit ?";  // begin: index_t, limit: uint64 -> next: index_t
	Query delete_OBJECT_begin_end_gc = "delete from OBJECT where id in (select id from OBJECT where id >= ? and id < ? and gc = ?)";  // begin: index_t, end: index_t, gc: bool -> void

public:
	DB(const char file[]) : Database(file) {
		if (ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
			Metadata metadata;
			Transaction([&]() {
				Execute(create_STATIC);
				Execute(create_OBJECT);
				Execute(create_SCAN);

				metadata.version = schema_version;
				metadata.gc_mark = false;
				metadata.gc_phase = GcPhase::Idle;
				metadata.block_count_last_gc = 0;
				metadata.root_index = ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc_mark);
				Execute(insert_STATIC_data, Serialize(metadata));
			});
			this->metadata = metadata;
		} else {
			metadata = Deserialize<Metadata>(ExecuteForOne<std::vector<byte>>(select_data_STATIC));
			if (metadata.version != schema_version) {
				throw std::runtime_error("metadata version doesn't match");
			}
		}
	}

private:
	Metadata metadata;
public:
	index_t get_root() { return metadata.root_index; }
private:
	void ExecuteUpdateMetadata(Metadata metadata) {
		Execute(update_STATIC_data, Serialize(metadata));
	}

private:
	constexpr static uint64 allocation_batch_size = 32;
private:
	std::vector<index_t> allocation_list;
public:
	index_t allocate_index() {
		if (allocation_list.empty()) {
			try {
				allocation_list.reserve(allocation_batch_size);
				Transaction([&]() {
					while (allocation_list.size() < allocation_batch_size) {
						allocation_list.emplace_back(ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc_mark));
					}
				});
			} catch (...) {
				allocation_list.clear();
				throw;
			}
		}
		index_t index = allocation_list.back(); allocation_list.pop_back();
		return index;
	}

public:
	std::vector<byte> read(index_t index) {
		return ExecuteForOne<std::vector<byte>>(select_data_OBJECT_id, index);
	}
	void write(index_t index, std::vector<byte> data, std::vector<index_t> ref) {
		Transaction([&]() {
			if (metadata.gc_phase != GcPhase::Scanning) {
				Execute(update_OBJECT_data_ref_id, data, ref, index);
			} else {
				bool gc = (bool)ExecuteForOne<uint64>(update_gc_OBJECT_data_ref_id, data, ref, index);
				if (gc == !metadata.gc_mark) {
					for (index_t id : ref) {
						Execute(insert_SCAN_id, id);
					}
				}
			}

			// error when sweeping writing with unmarked ref
		});
	}

private:
	constexpr static uint64 gc_scan_step_depth = 64;
	constexpr static uint64 gc_scan_changes_limit = 16 * 1024;
	constexpr static uint64 gc_scan_batch_size = 256;
	constexpr static uint64 gc_delete_batch_size = 256 * 1024;
public:
	void collect_garbage() {
		Metadata metadata = this->metadata;

		switch (metadata.gc_phase) {
		case GcPhase::Idle: goto idle;
		case GcPhase::Scanning: goto scanning;
		case GcPhase::Sweeping: goto sweeping;
		}

	idle:
		Transaction([&]() {
			Execute(insert_SCAN_id, metadata.root_index);
			metadata.block_count = ExecuteForOne<uint64>(select_count_OBJECT);
			metadata.block_count_marked = 0;
			metadata.gc_phase = GcPhase::Scanning;
			ExecuteUpdateMetadata(metadata);
		});
		this->metadata = metadata;

	scanning:
		for (;;) {
			bool finish = false;
			Transaction([&]() {
				uint64 changes = 0;
				for (uint64 i = 0; i < gc_scan_step_depth && changes < gc_scan_changes_limit; ++i) {
					std::vector<std::vector<index_t>> data_list = ExecuteForMultiple<std::vector<index_t>>(update_ref_OBJECT_gc, !metadata.gc_mark, gc_scan_batch_size, metadata.gc_mark);
					changes += Changes();
					Execute(delete_SCAN_limit, gc_scan_batch_size);
					for (auto& data : data_list) { for (index_t id : data) { Execute(insert_SCAN_id, id); } }
					if (ExecuteForOne<uint64>(select_count_SCAN) == 0) { finish = true; break; }
				}
				metadata.block_count_marked += changes;
				if (finish) {
					metadata.max_index = ExecuteForOne<index_t>(select_max_OBJECT);
					metadata.gc_delete_index = 0;
					metadata.gc_phase = GcPhase::Sweeping;
					ExecuteUpdateMetadata(metadata);
				}
			});
			this->metadata = metadata;
			if (finish) { break; }

			// interrupt
		}

	sweeping:
		for (;;) {
			bool finish = false;
			Transaction([&]() {
				index_t next = ExecuteForOne<index_t>(select_next_OBJECT_begin_limit, metadata.gc_delete_index, gc_delete_batch_size) + 1;
				Execute(delete_OBJECT_begin_end_gc, metadata.gc_delete_index, next, metadata.gc_mark);
				metadata.gc_delete_index = next;
				if (metadata.gc_delete_index > metadata.max_index) {
					finish = true;
					metadata.gc_mark = !metadata.gc_mark;
					metadata.gc_phase = GcPhase::Idle;
					metadata.block_count_last_gc = ExecuteForOne<uint64>(select_count_OBJECT);
				}
				ExecuteUpdateMetadata(metadata);
			});
			this->metadata = metadata;
			if (finish) { break; }

			// interrupt
		}
	}
};

std::unique_ptr<DB> pdb;
bool deserializing = false;

DB& db() {
	if (!pdb) {
		throw std::invalid_argument("file is not opened");
	}
	return *pdb;
}


END_NAMESPACE(Anonymous)


BEGIN_NAMESPACE(BlockStore)


void BlockManager::open_file(const char file[]) {
	if (pdb) {
		throw std::invalid_argument("open_file can only be called once");
	}
	pdb = std::make_unique<DB>(file);
}

block_ref BlockManager::get_root() { return db().get_root(); }

void BlockManager::collect_garbage() { return db().collect_garbage(); }

void BlockManager::begin_transaction() { db().BeginTransaction(); }

void BlockManager::commit() { db().Commit(); }

void BlockManager::rollback() { db().Rollback(); }


block_ref::block_ref() : index(deserializing ? 0 : db().allocate_index()) {}

void block_ref::deserialize_begin() { deserializing = true; }

void block_ref::deserialize_end() { deserializing = false; }

std::vector<byte> block_ref::read() const { return db().read(index); }

void block_ref::write(std::vector<byte> data, std::vector<index_t> ref) { return db().write(index, std::move(data), std::move(ref)); }


END_NAMESPACE(BlockStore)