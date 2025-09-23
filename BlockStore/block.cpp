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
	constexpr static uint64 schema_version = 2025'09'23'1;

	enum GcPhase : unsigned char { Idle, Scanning, Sweeping };

	struct Metadata {
		uint64 version;
		index_t root_index;
		bool root_initialized;
		bool gc_mark;
		GcPhase gc_phase;
		uint64 block_count_last_gc;
		uint64 block_count;
		uint64 block_count_marked;
		index_t max_index;
		index_t gc_delete_index;
	};

	constexpr static uint64 table_count = 4;
	constexpr static uint64 allocation_batch_size = 16;
	constexpr static uint64 gc_scan_step_depth = 64;
	constexpr static uint64 gc_scan_step_limit = 16 * 1024;
	constexpr static uint64 gc_buffer_batch_size = 256;
	constexpr static uint64 gc_delete_batch_size = 256 * 1024;

private:
	Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> count: uint64

	Query create_STATIC = "create table STATIC (data BLOB)";  // void -> void
	Query create_OBJECT = "create table OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB, ref BLOB)";  // void -> void
	Query create_EXPAND = "create table EXPAND (id INTEGER)";  // void -> void
	Query create_BUFFER = "create table BUFFER (id INTEGER)";  // void -> void

	Query insert_STATIC_data = "insert into STATIC values (?)";  // data: vector<byte> -> void
	Query select_data_STATIC = "select * from STATIC";  // void -> data: vector<byte>
	Query update_STATIC_data = "update STATIC set data = ?";  // data: vector<byte> -> void

	Query insert_id_OBJECT_gc = "insert into OBJECT (gc) values (?) returning id";  // gc: bool -> id: index_t
	Query delete_OBJECT_id = "delete from OBJECT where id = ?";  // id: index_t -> void

	Query select_data_OBJECT_id = "select data from OBJECT where id = ?";  // id: index_t -> data: vector<byte>
	Query update_OBJECT_data_ref_id = "update OBJECT set data = ?, ref = ? where id = ?";  // data: vector<byte>, ref: vector<index_t>, id: index_t -> void
	Query update_gc_OBJECT_data_ref_id = "update OBJECT set data = ?, ref = ? where id = ? returning gc";  // data: vector<byte>, ref: vector<index_t>, id: index_t -> gc: bool

	Query insert_BUFFER_limit = "insert into BUFFER select * from EXPAND order by rowid desc limit ?";  // limit: uint64 -> void
	Query select_count_BUFFER = "select count(*) from BUFFER";  // void -> count: uint64
	Query delete_EXPAND_limit = "delete from EXPAND where rowid in (select rowid from EXPAND order by rowid desc limit ?)";  // limit: uint64 -> void
	Query select_ref_OBJECT_gc = "select ref from OBJECT where id in (select * from BUFFER) and gc = ?";  // gc: bool -> vector<ref: vector<index_t>>
	Query insert_EXPAND_id = "insert into EXPAND values (?)";  // id: index_t -> void
	Query update_OBJECT_gc = "update OBJECT set gc = ? where id in (select * from BUFFER)";  // gc: bool -> void
	Query delete_BUFFER = "delete from BUFFER";  // void -> void

	Query select_count_OBJECT = "select count(*) from OBJECT";  // void -> count: uint64
	Query select_max_OBJECT = "select max(id) from OBJECT";  // void -> id: index_t
	Query select_next_OBJECT_begin_limit = "select max(id) from OBJECT where id >= ? order by id asc limit ?";  // begin: index_t, limit: uint64 -> next: index_t
	Query delete_OBJECT_begin_end_gc = "delete from OBJECT where id in (select id from OBJECT where id >= ? and id < ? and gc = ?)";  // begin: index_t, end: index_t, gc: bool -> void

public:
	DB(const char file[]) : Database(file), metadata() {
		if (ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
			Transaction([&]() {
				Execute(create_STATIC);
				Execute(create_OBJECT);
				Execute(create_EXPAND);
				Execute(create_BUFFER);

				metadata.version = schema_version;
				metadata.gc_mark = false;
				metadata.gc_phase = GcPhase::Idle;
				metadata.block_count_last_gc = 0;
				metadata.root_index = ExecuteInsertOne();
				metadata.root_initialized = false;
				Execute(insert_STATIC_data, Serialize(metadata));
			});
		} else {
			metadata = Deserialize<Metadata>(ExecuteForOne<std::vector<byte>>(select_data_STATIC));
			if (metadata.version != schema_version) {
				throw std::runtime_error("metadata version doesn't match");
			}
			if (!metadata.root_initialized) {
				new_index_set.emplace(metadata.root_index);
			}
		}
	}
	~DB() {
		Transaction([&]() {
			if (!metadata.root_initialized) {
				if (!new_index_set.contains(metadata.root_index)) {
					metadata.root_initialized = true;
					ExecuteUpdateMetadata();
				} else {
					new_index_set.erase(metadata.root_index);
				}
			}
			for (index_t index : new_index_set) {
				Execute(delete_OBJECT_id, index);
			}
			for (index_t index : allocation_list) {
				Execute(delete_OBJECT_id, index);
			}
		});
	}

private:
	Metadata metadata;
private:
	void ExecuteUpdateMetadata() {
		Execute(update_STATIC_data, Serialize(metadata));
	}
	index_t ExecuteInsertOne() {
		return ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc_mark);
	}
public:
	index_t get_root() {
		return metadata.root_index;
	}

private:
	std::vector<index_t> allocation_list;
	std::unordered_set<index_t> new_index_set;
public:
	index_t allocate_index() {
		if (allocation_list.empty()) {
			try {
				allocation_list.reserve(allocation_batch_size);
				Transaction([&]() {
					while (allocation_list.size() < allocation_batch_size) {
						allocation_list.emplace_back(ExecuteInsertOne());
					}
				});
			} catch (...) {
				allocation_list.clear();
				throw;
			}
		}
		index_t index = allocation_list.back(); allocation_list.pop_back();
		new_index_set.emplace(index);
		return index;
	}

public:
	void transaction(std::function<void()> op) {
		Transaction(std::move(op));
	}
public:
	std::vector<byte> read(index_t index) {
		if (new_index_set.contains(index)) {
			return {};
		} else {
			return ExecuteForOne<std::vector<byte>>(select_data_OBJECT_id, index);
		}
	}
	void write(index_t index, std::vector<byte> data, std::vector<index_t> ref) {
		new_index_set.erase(index);
		Transaction([&]() {
			if (metadata.gc_phase != GcPhase::Scanning) {
				Execute(update_OBJECT_data_ref_id, data, ref, index);
			} else {
				bool gc = (bool)ExecuteForOne<uint64>(update_gc_OBJECT_data_ref_id, data, ref, index);
				if (gc == !metadata.gc_mark) {
					for (index_t id : ref) {
						Execute(insert_EXPAND_id, id);
					}
				}
			}
		});
	}

public:
	void collect_garbage() {
		switch (metadata.gc_phase) {
		case GcPhase::Idle: goto idle;
		case GcPhase::Scanning: goto scanning;
		case GcPhase::Sweeping: goto sweeping;
		}
	idle:
		Transaction([&]() {
			Execute(insert_EXPAND_id, metadata.root_index);
			metadata.block_count = ExecuteForOne<uint64>(select_count_OBJECT);
			metadata.block_count_marked = 0;
			metadata.gc_phase = GcPhase::Scanning;
			ExecuteUpdateMetadata();
		});

	scanning:
		for (;;) {
			bool finish = false;
			Transaction([&]() {
				uint64 changes = 0;
				for (uint64 i = 0; i < gc_scan_step_depth && changes < gc_scan_step_limit; ++i) {
					Execute(insert_BUFFER_limit, gc_buffer_batch_size);
					if (ExecuteForOne<uint64>(select_count_BUFFER) == 0) {
						finish = true;
						metadata.max_index = ExecuteForOne<index_t>(select_max_OBJECT);
						metadata.gc_delete_index = 0;
						metadata.gc_phase = GcPhase::Sweeping;
						ExecuteUpdateMetadata();
						return;
					}
					Execute(delete_EXPAND_limit, gc_buffer_batch_size);
					std::vector<std::vector<index_t>> data_list = ExecuteForMultiple<std::vector<index_t>>(select_ref_OBJECT_gc, metadata.gc_mark);
					for (auto& data : data_list) { for (index_t id : data) { Execute(insert_EXPAND_id, id); } }
					Execute(update_OBJECT_gc, !metadata.gc_mark);
					changes += Changes();
					Execute(delete_BUFFER);
				}
				metadata.block_count_marked += changes;
			});
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
				ExecuteUpdateMetadata();
			});
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

void BlockManager::transaction(std::function<void(void)> op) { return db().transaction(std::move(op)); }

void BlockManager::collect_garbage() { return db().collect_garbage(); }


block_ref::block_ref() : index(deserializing ? 0 : db().allocate_index()) {}

void block_ref::deserialize_begin() { deserializing = true; }

void block_ref::deserialize_end() { deserializing = false; }

std::vector<byte> block_ref::read() { return db().read(index); }

void block_ref::write(std::vector<byte> data, std::vector<index_t> ref) { return db().write(index, std::move(data), std::move(ref)); }


END_NAMESPACE(BlockStore)