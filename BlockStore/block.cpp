#include "block_manager.h"
#include "block_cache.h"

#include "CppSerialize/serializer.h"
#include "SQLite3Helper/sqlite3_helper.h"

#include <cassert>
#include <memory>


BEGIN_NAMESPACE(BlockStore)

struct block_ref_access {
	static block_ref construct(index_t index) { return block_ref(index); }
	static size_t count_object() { return block_ref::GetCount(); }
};


size_t block_ref::ObjectCount::count;

size_t block_cache_shared::ObjectCount::count;


BEGIN_NAMESPACE(Anonymous)

using namespace SQLite3Helper;
using namespace CppSerialize;


class DB : public Database {
private:
	constexpr static uint64 schema_version = 2025'09'27'0;

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
				Execute(insert_STATIC_data, Serialize(metadata).Get());
			});
			this->metadata = metadata;
		} else {
			metadata = Deserialize<Metadata>(ExecuteForOne<std::vector<byte>>(select_data_STATIC)).Get();
			if (metadata.version != schema_version) {
				throw std::runtime_error("metadata version doesn't match");
			}
		}
	}

private:
	Metadata metadata;
public:
	block_ref get_root() { return block_ref_access::construct(metadata.root_index); }
private:
	void ExecuteUpdateMetadata(Metadata metadata) {
		Execute(update_STATIC_data, Serialize(metadata).Get());
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
						allocation_list.emplace_back(ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc_phase == GcPhase::Sweeping ? !metadata.gc_mark : metadata.gc_mark));
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
	void write(index_t index, const std::vector<byte>& data, const std::vector<index_t>& ref) {
		Transaction([&]() {
			if (metadata.gc_phase != GcPhase::Scanning) {
				Execute(update_OBJECT_data_ref_id, data, ref, index);
				assert(Changes() > 0);
			} else {
				bool gc = (bool)ExecuteForOne<uint64>(update_gc_OBJECT_data_ref_id, data, ref, index);
				if (gc == !metadata.gc_mark) {
					for (index_t id : ref) {
						Execute(insert_SCAN_id, id);
					}
				}
			}
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
					if (ExecuteForOne<uint64>(select_count_SCAN) == 0) { finish = true; break; }
					std::vector<std::vector<index_t>> data_list = ExecuteForMultiple<std::vector<index_t>>(update_ref_OBJECT_gc, !metadata.gc_mark, gc_scan_batch_size, metadata.gc_mark);
					changes += Changes();
					Execute(delete_SCAN_limit, gc_scan_batch_size);
					for (auto& data : data_list) { for (index_t id : data) { Execute(insert_SCAN_id, id); } }
				}
				metadata.block_count_marked += changes;
				if (finish) {
					if (block_ref_access::count_object() > 0) {
						return;
					}
					allocation_list.clear();
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
		if (metadata.gc_phase != GcPhase::Sweeping) {
			return;
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


class BlockCacheMap : public TransactionHook {
private:
	using move_assign_fn = void(*)(std::any&, std::any&);
	using write_fn = void(*)(const block_ref&, const std::any&);
	using map_value_tuple = std::tuple<std::any, move_assign_fn, write_fn>;

private:
	std::unordered_map<index_t, map_value_tuple> map;
	std::unordered_map<index_t, std::any> map_copy;

public:
	bool has(index_t index) {
		return map.contains(index);
	}
	const std::any& get(index_t index) {
		return std::get<std::any>(map.at(index));
	}
	const std::any& set(index_t index, map_value_tuple value) {
		return std::get<std::any>(map.emplace(index, std::move(value)).first->second);
	}
	std::any& update(index_t index) {
		if (!map_copy.contains(index)) {
			map_copy.emplace(index, get(index));
		}
		return std::get<std::any>(map.at(index));
	}
public:
	void clear() {
		assert(map_copy.empty());
		map.clear();
	}

private:
	virtual void AfterBeginTransaction() override {
		if (!map_copy.empty()) {
			throw std::invalid_argument("block cache operations not wrapped in a transaction");
		}
	}
	virtual void BeforeCommit() override {
		for (const auto& [index, _] : map_copy) {
			const auto& [object, _, write] = map[index];
			write(block_ref_access::construct(index), object);
		}
	}
	virtual void AfterCommit() override {
		map_copy.clear();
	}
	virtual void AfterRollback() override {
		for (auto& [index, value] : map_copy) {
			auto& [object, assign, _] = map[index];
			assign(object, value);
		}
		map_copy.clear();
	}
};

BlockCacheMap block_cache_map;


END_NAMESPACE(Anonymous)


void BlockManager::open_file(const char file[]) {
	if (pdb) {
		throw std::invalid_argument("open_file can only be called once");
	}
	pdb = std::make_unique<DB>(file);
	pdb->SetTransactionHook(block_cache_map);
}

block_ref BlockManager::get_root() { return db().get_root(); }

void BlockManager::collect_garbage() { return db().collect_garbage(); }

void BlockManager::begin_transaction() { db().BeginTransaction(); }

void BlockManager::commit() { db().Commit(); }

void BlockManager::rollback() { db().Rollback(); }


block_ref::block_ref(index_t index) : index(index) {}

block_ref::block_ref() : block_ref(deserializing ? 0 : db().allocate_index()) {}

void block_ref::deserialize_begin() { deserializing = true; }

void block_ref::deserialize_end() { deserializing = false; }

std::vector<byte> block_ref::read() const { return db().read(index); }

void block_ref::write(const std::vector<byte>& data, const std::vector<index_t>& ref) { return db().write(index, data, ref); }


bool block_cache_shared::has(index_t index) { return block_cache_map.has(index); }

const std::any& block_cache_shared::get(index_t index) { return block_cache_map.get(index); }

const std::any& block_cache_shared::set(index_t index, map_value value) { return block_cache_map.set(index, std::move(value)); }

std::any& block_cache_shared::update(index_t index) { return block_cache_map.update(index); }

void block_cache_shared::clear() {
	if (GetCount() > 0) {
		throw std::invalid_argument("cannot clear block cache with active instances");
	}
	return block_cache_map.clear();
}


END_NAMESPACE(BlockStore)