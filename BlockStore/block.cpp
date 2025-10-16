#include "block_cache.h"

#include "CppSerialize/serializer.h"
#include "SQLite3Helper/sqlite3_helper.h"

#include <cassert>
#include <memory>
#include <unordered_map>
#include <unordered_set>


BEGIN_NAMESPACE(BlockStore)

struct block_ref_access {
	static block_ref construct(index_t index) { return block_ref(index); }
	static size_t count_object() { return block_ref::GetCount(); }
};


size_t block_ref::ObjectCount::count;

size_t block_cache_shared::ObjectCount::count;

BlockManager::GCCallback BlockManager::default_gc_callback;


BEGIN_NAMESPACE(Anonymous)

using namespace SQLite3Helper;
using namespace CppSerialize;


class DB : public Database {
private:
	constexpr static uint64 schema_version = 2025'09'27'0;

	using GCPhase = BlockManager::GCPhase;
	using GCInfo = BlockManager::GCInfo;

	struct Metadata {
		uint64 version = schema_version;
		index_t root_index;
		GCInfo gc;
	};
	static_assert(layout_trivial<Metadata>);
	static_assert(sizeof(Metadata) == 64);

private:
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

				metadata.root_index = ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc.mark);
				Execute(insert_STATIC_data, Serialize(metadata).Get());
			});
			this->metadata = metadata;
		} else {
			metadata = Deserialize<Metadata>(ExecuteForOne<std::vector<byte>>(select_data_STATIC)).Get();
			if (metadata.version != schema_version) {
				// upgrade
				throw std::runtime_error("unsupported database version");
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
						allocation_list.emplace_back(ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc.phase == GCPhase::Sweeping ? !metadata.gc.mark : metadata.gc.mark));
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
			if (metadata.gc.phase != GCPhase::Scanning) {
				Execute(update_OBJECT_data_ref_id, data, ref, index);
				assert(Changes() > 0);
			} else {
				bool gc = (bool)ExecuteForOne<uint64>(update_gc_OBJECT_data_ref_id, data, ref, index);
				if (gc == !metadata.gc.mark) {
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
	const GCInfo& get_gc_info() {
		GCInfo& info = metadata.gc;
		if (info.phase == GCPhase::Idle) {
			info.block_count = ExecuteForOne<uint64>(select_count_OBJECT);
			info.block_count_marked = 0;
		}
		return info;
	}
	void gc(BlockManager::GCCallback& callback) {
		Metadata metadata = this->metadata;

		switch (metadata.gc.phase) {
		case GCPhase::Idle: goto idle;
		case GCPhase::Scanning: goto scanning;
		case GCPhase::Sweeping: goto sweeping;
		}

	idle:
		Transaction([&]() {
			Execute(insert_SCAN_id, metadata.root_index);

			metadata.gc.block_count = ExecuteForOne<uint64>(select_count_OBJECT);
			metadata.gc.block_count_marked = 0;

			callback.Notify(metadata.gc);

			metadata.gc.phase = GCPhase::Scanning;
			ExecuteUpdateMetadata(metadata);
		});
		this->metadata = metadata;

	scanning:
		for (;;) {
			if (callback.Interrupt(metadata.gc)) {
				return;
			}

			bool finish = false;

			Transaction([&]() {
				uint64 changes = 0;
				for (uint64 i = 0; i < gc_scan_step_depth && changes < gc_scan_changes_limit; ++i) {
					if (ExecuteForOne<uint64>(select_count_SCAN) == 0) {
						finish = true;
						break;
					}
					std::vector<std::vector<index_t>> data_list = ExecuteForMultiple<std::vector<index_t>>(update_ref_OBJECT_gc, !metadata.gc.mark, gc_scan_batch_size, metadata.gc.mark);
					changes += Changes();
					Execute(delete_SCAN_limit, gc_scan_batch_size);
					for (auto& data : data_list) { for (index_t id : data) { Execute(insert_SCAN_id, id); } }
				}
				metadata.gc.block_count_marked += changes;

				if (finish) {
					callback.Notify(metadata.gc);

					if (block_ref_access::count_object() > 0) {
						return;
					}

					allocation_list.clear();
					metadata.gc.max_index = ExecuteForOne<index_t>(select_max_OBJECT);
					metadata.gc.sweeping_index = 0;
					metadata.gc.phase = GCPhase::Sweeping;
					ExecuteUpdateMetadata(metadata);
				}
			});
			this->metadata = metadata;

			if (finish) {
				break;
			}
		}
		if (metadata.gc.phase != GCPhase::Sweeping) {
			return;
		}

	sweeping:
		for (;;) {
			if (callback.Interrupt(metadata.gc)) {
				return;
			}

			bool finish = false;

			Transaction([&]() {
				index_t next = ExecuteForOne<index_t>(select_next_OBJECT_begin_limit, metadata.gc.sweeping_index, gc_delete_batch_size) + 1;
				Execute(delete_OBJECT_begin_end_gc, metadata.gc.sweeping_index, next, metadata.gc.mark);
				metadata.gc.sweeping_index = next;
				if (metadata.gc.sweeping_index > metadata.gc.max_index) {
					finish = true;

					callback.Notify(metadata.gc);

					metadata.gc.mark = !metadata.gc.mark;
					metadata.gc.phase = GCPhase::Idle;
					metadata.gc.block_count_prev = ExecuteForOne<uint64>(select_count_OBJECT);
					metadata.gc.block_count = metadata.gc.block_count_prev;
					metadata.gc.block_count_marked = 0;
				}
				ExecuteUpdateMetadata(metadata);
			});
			this->metadata = metadata;

			if (finish) {
				break;
			}
		}

		callback.Notify(metadata.gc);
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
	using write_fn = void(*)(const block_ref&, const std::any&);
	using map_value = std::tuple<std::any, write_fn>;

private:
	std::unordered_map<index_t, map_value> map;
	std::unordered_set<index_t> dirty;

public:
	bool has(index_t index) {
		return map.contains(index);
	}
	std::any& get(index_t index) {
		return std::get<std::any>(map.at(index));
	}
	std::any& set(index_t index, map_value value) {
		return std::get<std::any>(map.insert_or_assign(index, std::move(value)).first->second);
	}
	void mark(index_t index) {
		dirty.emplace(index);
	}
public:
	void clear() {
		assert(dirty.empty());
		map.clear();
	}

private:
	virtual void AfterBeginTransaction() override {
		if (!dirty.empty()) {
			throw std::invalid_argument("block cache operations not committed before transaction");
		}
	}
	virtual void BeforeCommit() override {
		for (index_t index : dirty) {
			const auto& [object, write] = map.at(index);
			write(block_ref_access::construct(index), object);
		}
	}
	virtual void AfterCommit() override {
		dirty.clear();
	}
	virtual void AfterRollback() override {}
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

void BlockManager::begin_transaction() { db().BeginTransaction(); }

void BlockManager::commit() { db().Commit(); }

void BlockManager::rollback() { db().Rollback(); }

const BlockManager::GCInfo& BlockManager::get_gc_info() { return db().get_gc_info(); }

void BlockManager::gc(GCCallback& callback) { return db().gc(callback); }


block_ref::block_ref(index_t index) : index(index) {}

block_ref::block_ref() : block_ref(deserializing ? 0 : db().allocate_index()) {}

void block_ref::deserialize_begin() { deserializing = true; }

void block_ref::deserialize_end() { deserializing = false; }

std::vector<byte> block_ref::read() const { return db().read(index); }

void block_ref::write(const std::vector<byte>& data, const std::vector<index_t>& ref) { return db().write(index, data, ref); }


bool block_cache_shared::has(index_t index) { return block_cache_map.has(index); }

std::any& block_cache_shared::get(index_t index) { return block_cache_map.get(index); }

std::any& block_cache_shared::set(index_t index, map_value value) { return block_cache_map.set(index, std::move(value)); }

void block_cache_shared::mark(index_t index) { return block_cache_map.mark(index); }

void block_cache_shared::clear() {
	if (GetCount() > 0) {
		throw std::invalid_argument("cannot clear block cache with active instances");
	}
	return block_cache_map.clear();
}


END_NAMESPACE(BlockStore)