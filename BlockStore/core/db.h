#pragma once

#include "gc.h"
#include "CppSerialize/serializer.h"
#include "SQLite3Helper/sqlite3_helper.h"

#include <unordered_map>
#include <cassert>


namespace BlockStore {

using namespace SQLite3Helper;
using namespace CppSerialize;


class DB : public Database {
private:
	constexpr static uint64 schema_version = 2026'02'20'00;

	struct Metadata {
		uint64 version = schema_version;
		ref_t root_ref;
		GCInfo gc;
	};
	static_assert(sizeof(Metadata) == 64);
	static_assert(layout_trivial<Metadata>);

private:
	Query create_META = "create table META (data BLOB)";  // void -> void
	Query create_BLOCK = "create table BLOCK (id INTEGER primary key, gc BOOLEAN, data BLOB, ref BLOB)";  // void -> void
	Query create_SCAN = "create table SCAN (id INTEGER)";  // void -> void

	Query insert_META_data = "insert into META values (?)";  // data: vector<byte> -> void
	Query select_data_META = "select * from META";  // void -> data: vector<byte>
	Query update_META_data = "update META set data = ?";  // data: vector<byte> -> void

	Query insert_id_BLOCK_gc = "insert into BLOCK (gc) values (?) returning id";  // gc: bool -> id: ref_t

	Query select_data_BLOCK_id = "select data from BLOCK where id = ?";  // id: ref_t -> data: vector<byte>
	Query update_BLOCK_data_ref_id = "update BLOCK set data = ?, ref = ? where id = ?";  // data: vector<byte>, ref: vector<ref_t>, id: ref_t -> void

	Query select_exists_SCAN = "select exists(select 1 from SCAN limit 1)";  // void -> exists: bool
	Query update_ref_BLOCK_gc = "update BLOCK set gc = ? where id in (select id from SCAN order by rowid desc limit ?) and gc = ? returning ref";  // gc: bool, limit: uint64, gc: bool -> vector<ref: vector<ref_t>>
	Query delete_SCAN_limit = "delete from SCAN where rowid in (select rowid from SCAN order by rowid desc limit ?)";  // limit: uint64 -> void
	Query insert_SCAN_id = "insert into SCAN values (?)";  // id: ref_t -> void

	Query select_max_BLOCK = "select max(id) from BLOCK";  // void -> id: ref_t
	Query select_end_BLOCK_begin_offset = "select id from BLOCK where id > ? order by id asc limit 1 offset ?";  // begin: ref_t, offset: uint64 -> end: ref_t
	Query delete_BLOCK_begin_end_gc = "delete from BLOCK where id in (select id from BLOCK where id >= ? and id < ? and gc = ?)";  // begin: ref_t, end: ref_t, gc: bool -> void

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

				metadata.root_ref = ExecuteForOne<uint64>(insert_id_BLOCK_gc, metadata.gc.mark);
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
	ref_t get_root() { return metadata.root_ref; }
private:
	void ExecuteUpdateMetadata(Metadata metadata) {
		Execute(update_META_data, Serialize(metadata).Get());
	}

private:
	constexpr static uint64 allocation_batch_size = 32;
	static_assert(allocation_batch_size > 0);
private:
	std::vector<ref_t> allocation_list;
public:
	ref_t allocate() {
		if (allocation_list.empty()) {
			Metadata metadata = this->metadata;
			std::vector<ref_t> allocation_list; allocation_list.reserve(allocation_batch_size);
			Transaction([&]() {
				for (size_t i = 0; i < allocation_batch_size; ++i) {
					allocation_list.emplace_back(ExecuteForOne<uint64>(insert_id_BLOCK_gc, metadata.gc.phase == GCPhase::Sweeping ? !metadata.gc.mark : metadata.gc.mark));
				}
				metadata.gc.block_count += allocation_batch_size;
				ExecuteUpdateMetadata(metadata);
			});
			this->metadata = metadata;
			this->allocation_list = std::move(allocation_list);
		}
		ref_t ref = allocation_list.back(); allocation_list.pop_back();
		return ref;
	}

private:
	std::unordered_map<ref_t, size_t> active_ref_set;
	std::vector<ref_t> new_ref_list;
public:
	void inc_ref(ref_t ref) {
		if (auto it = active_ref_set.find(ref); it != active_ref_set.end()) {
			it->second++;
		} else {
			active_ref_set.emplace(ref, 1);
			if (metadata.gc.phase == GCPhase::Scanning) {
				new_ref_list.push_back(ref);
			}
		}
	}
	void dec_ref(ref_t ref) {
		auto it = active_ref_set.find(ref);
		assert(it != active_ref_set.end());
		if (it->second > 1) {
			it->second--;
		} else {
			active_ref_set.erase(it);
		}
	}

public:
	std::vector<byte> read(ref_t id) {
		return ExecuteForOne<std::vector<byte>>(select_data_BLOCK_id, id);
	}
	void write(ref_t id, const std::vector<byte>& data, const std::vector<ref_t>& ref_list) {
		Execute(update_BLOCK_data_ref_id, data, ref_list, id);
	}

public:
	const GCInfo& get_gc_info() {
		return metadata.gc;
	}
	void gc(const GCOption& option) {
		option.check();

		Metadata metadata = this->metadata;

		switch (metadata.gc.phase) {
		case GCPhase::Idle: goto idle;
		case GCPhase::Scanning: goto scanning;
		case GCPhase::Sweeping: goto sweeping;
		}

	idle:
		Transaction([&]() {
			if (!active_ref_set.contains(metadata.root_ref)) {
				Execute(insert_SCAN_id, metadata.root_ref);
			}
			for (const auto& pair : active_ref_set) {
				Execute(insert_SCAN_id, pair.first);
			}

			metadata.gc.phase = GCPhase::Scanning;
			ExecuteUpdateMetadata(metadata);
		});
		this->metadata = metadata;
		option.callback(metadata.gc);

	scanning:
		for (;;) {
			bool finish = false;

			Transaction([&]() {
				for (auto id : new_ref_list) {
					Execute(insert_SCAN_id, id);
				}

				uint64 changes = 0;
				for (uint64 i = 0; i < option.scan_step_depth && changes < option.scan_changes_limit; ++i) {
					if (ExecuteForOne<uint64>(select_exists_SCAN) == 0) {
						finish = true;
						break;
					}
					std::vector<std::vector<ref_t>> data_list = ExecuteForMultiple<std::vector<ref_t>>(update_ref_BLOCK_gc, !metadata.gc.mark, option.scan_batch_size, metadata.gc.mark);
					changes += Changes();
					Execute(delete_SCAN_limit, option.scan_batch_size);
					for (auto& data : data_list) { for (ref_t id : data) { Execute(insert_SCAN_id, id); } }
				}
				metadata.gc.block_count_marked += changes;

				if (finish) {
					metadata.gc.phase = GCPhase::Sweeping;
					metadata.gc.sweeping_id = 0;
					ExecuteUpdateMetadata(metadata);
				}
			});
			this->metadata = metadata;
			new_ref_list.clear();

			if (finish) {
				allocation_list.clear();
				option.callback(metadata.gc);
				break;
			}

			if (option.callback(metadata.gc)) {
				return;
			}
		}

	sweeping:
		for (;;) {
			bool finish = false;

			Transaction([&]() {
				metadata.gc.max_id = ExecuteForOne<ref_t>(select_max_BLOCK);
				ref_t end = ExecuteForOneOptional<ref_t>(select_end_BLOCK_begin_offset, metadata.gc.sweeping_id, option.delete_batch_size - 1).value_or(metadata.gc.max_id + 1);
				Execute(delete_BLOCK_begin_end_gc, metadata.gc.sweeping_id, end, metadata.gc.mark);
				metadata.gc.block_count -= Changes();
				metadata.gc.sweeping_id = end;
				if (metadata.gc.sweeping_id > metadata.gc.max_id) {
					finish = true;

					metadata.gc.mark = !metadata.gc.mark;
					metadata.gc.phase = GCPhase::Idle;
					metadata.gc.block_count_prev = metadata.gc.block_count;
					metadata.gc.block_count_marked = 0;
				}
				ExecuteUpdateMetadata(metadata);
			});
			this->metadata = metadata;

			if (finish) {
				option.callback(metadata.gc);
				break;
			}

			if (option.callback(metadata.gc)) {
				return;
			}
		}
	}
};


} // namespace BlockStore
