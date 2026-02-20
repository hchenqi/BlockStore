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
	constexpr static uint64 schema_version = 2026'02'20'00;

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
	Query create_META = "create table META (data BLOB)";  // void -> void
	Query create_BLOCK = "create table BLOCK (id INTEGER primary key, gc BOOLEAN, data BLOB, ref BLOB)";  // void -> void
	Query create_SCAN = "create table SCAN (id INTEGER)";  // void -> void

	Query insert_META_data = "insert into META values (?)";  // data: vector<byte> -> void
	Query select_data_META = "select * from META";  // void -> data: vector<byte>
	Query update_META_data = "update META set data = ?";  // data: vector<byte> -> void

	Query insert_id_BLOCK_gc = "insert into BLOCK (gc) values (?) returning id";  // gc: bool -> id: index_t

	Query select_data_BLOCK_id = "select data from BLOCK where id = ?";  // id: index_t -> data: vector<byte>
	Query update_BLOCK_data_ref_id = "update BLOCK set data = ?, ref = ? where id = ?";  // data: vector<byte>, ref: vector<index_t>, id: index_t -> void
	Query update_gc_BLOCK_data_ref_id = "update BLOCK set data = ?, ref = ? where id = ? returning gc";  // data: vector<byte>, ref: vector<index_t>, id: index_t -> gc: bool

	Query select_exists_SCAN = "select exists(select 1 from SCAN limit 1)";  // void -> exists: bool
	Query update_ref_BLOCK_gc = "update BLOCK set gc = ? where id in (select id from SCAN order by rowid desc limit ?) and gc = ? returning ref";  // gc: bool, limit: uint64, gc: bool -> vector<ref: vector<index_t>>
	Query delete_SCAN_limit = "delete from SCAN where rowid in (select rowid from SCAN order by rowid desc limit ?)";  // limit: uint64 -> void
	Query insert_SCAN_id = "insert into SCAN values (?)";  // id: index_t -> void

	Query select_max_BLOCK = "select max(id) from BLOCK";  // void -> id: index_t
	Query select_end_BLOCK_begin_offset = "select id from BLOCK where id > ? order by id asc limit 1 offset ?";  // begin: index_t, offset: uint64 -> end: index_t
	Query delete_BLOCK_begin_end_gc = "delete from BLOCK where id in (select id from BLOCK where id >= ? and id < ? and gc = ?)";  // begin: index_t, end: index_t, gc: bool -> void

public:
	DB(const char file[]) : Database(file) {
		try {
			this->metadata = Deserialize<Metadata>(ExecuteForOne<std::vector<byte>>(select_data_META)).Get();
		} catch (...) {
			Metadata metadata;
			Transaction([&]() {
				Execute(create_META);
				Execute(create_BLOCK);
				Execute(create_SCAN);

				metadata.root_index = ExecuteForOne<uint64>(insert_id_BLOCK_gc, metadata.gc.mark);
				metadata.gc.block_count++;
				Execute(insert_META_data, Serialize(metadata).Get());
			});
			this->metadata = metadata;
		}
		if (this->metadata.version != schema_version) {
			// upgrade
			throw std::runtime_error("unsupported database version");
		}
	}

private:
	Metadata metadata;
public:
	block_ref get_root() { return block_ref_access::construct(metadata.root_index); }
private:
	void ExecuteUpdateMetadata(Metadata metadata) {
		Execute(update_META_data, Serialize(metadata).Get());
	}

private:
	constexpr static uint64 allocation_batch_size = 32;
	static_assert(allocation_batch_size > 0);
private:
	std::vector<index_t> allocation_list;
public:
	index_t allocate_index() {
		if (allocation_list.empty()) {
			try {
				Metadata metadata = this->metadata;
				allocation_list.reserve(allocation_batch_size);
				Transaction([&]() {
					while (allocation_list.size() < allocation_batch_size) {
						allocation_list.emplace_back(ExecuteForOne<uint64>(insert_id_BLOCK_gc, metadata.gc.phase == GCPhase::Sweeping ? !metadata.gc.mark : metadata.gc.mark));
					}
					metadata.gc.block_count += allocation_batch_size;
					ExecuteUpdateMetadata(metadata);
				});
				this->metadata = metadata;
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
		return ExecuteForOne<std::vector<byte>>(select_data_BLOCK_id, index);
	}
	void write(index_t index, const std::vector<byte>& data, const std::vector<index_t>& ref) {
		Transaction([&]() {
			if (metadata.gc.phase != GCPhase::Scanning) {
				Execute(update_BLOCK_data_ref_id, data, ref, index);
				assert(Changes() > 0);
			} else {
				bool gc = (bool)ExecuteForOne<uint64>(update_gc_BLOCK_data_ref_id, data, ref, index);
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

	static_assert(gc_scan_step_depth > 0);
	static_assert(gc_scan_changes_limit > 0);
	static_assert(gc_scan_batch_size > 0);
	static_assert(gc_delete_batch_size > 0);

public:
	const GCInfo& get_gc_info() {
		return metadata.gc;
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
					if (ExecuteForOne<uint64>(select_exists_SCAN) == 0) {
						finish = true;
						break;
					}
					std::vector<std::vector<index_t>> data_list = ExecuteForMultiple<std::vector<index_t>>(update_ref_BLOCK_gc, !metadata.gc.mark, gc_scan_batch_size, metadata.gc.mark);
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
					metadata.gc.max_index = ExecuteForOne<index_t>(select_max_BLOCK);
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
				index_t end = ExecuteForOneOptional<index_t>(select_end_BLOCK_begin_offset, metadata.gc.sweeping_index, gc_delete_batch_size - 1).value_or(metadata.gc.max_index + 1);
				Execute(delete_BLOCK_begin_end_gc, metadata.gc.sweeping_index, end, metadata.gc.mark);
				metadata.gc.block_count -= Changes();
				metadata.gc.sweeping_index = end;
				if (metadata.gc.sweeping_index > metadata.gc.max_index) {
					finish = true;

					callback.Notify(metadata.gc);

					metadata.gc.mark = !metadata.gc.mark;
					metadata.gc.phase = GCPhase::Idle;
					metadata.gc.block_count_prev = metadata.gc.block_count;
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