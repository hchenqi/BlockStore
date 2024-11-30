#include "block_manager.h"

#include "CppSerialize/cpp_serialize.h"

#include "Sqlite3/sqlite_helper.h"

#include <memory>


#pragma comment(lib, "Sqlite3.lib")


BEGIN_NAMESPACE(BlockStore)

enum GcPhase : unsigned char { Idle, Scanning, Sweeping };

struct Metadata {
	uint64 version;
	index_t root_index;
	bool gc_mark;
	GcPhase gc_phase;
	uint64 block_count_last_gc;
	index_t max_index;
	index_t gc_delete_index;
};

constexpr uint64 metadata_version = 2024'11'29'00;


BEGIN_NAMESPACE(Anonymous)

using namespace SqliteHelper;

std::unique_ptr<Database> db;
Metadata metadata;

constexpr uint64 table_count = 4;
constexpr uint64 gc_buffer_batch_size = 256;
constexpr uint64 gc_delete_batch_size = 256 * 1024;

Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> count: uint64

Query create_STATIC = "create table STATIC (data BLOB)";  // void -> void
Query create_OBJECT = "create table OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB, ref BLOB)";  // void -> void
Query create_EXPAND = "create table EXPAND (id INTEGER)";  // void -> void
Query create_BUFFER = "create table BUFFER (id INTEGER)";  // void -> void

Query insert_STATIC_data = "insert into STATIC values (?)";  // data: vector<byte> -> void
Query select_data_STATIC = "select * from STATIC";  // void -> data: vector<byte>
Query update_STATIC_data = "update STATIC set data = ?";  // data: vector<byte> -> void

Query insert_id_OBJECT_gc_data_ref = "insert into OBJECT (gc, data, ref) values (?, ?, ?) returning id";  // gc: bool, data: vector<byte>, ref: vector<index_t> -> id: index_t
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
Query select_next_OBJECT_begin_limit = "select max(id) from OBJECT where id > ? order by id asc limit ?";  // begin: index_t, limit: uint64 -> next: index_t
Query delete_OBJECT_begin_gc_limit = "delete from OBJECT where id in (select id from OBJECT where id > ? and gc = ? order by id asc limit ?)";  // begin: index_t, gc: bool, limit: uint64 -> void

inline void UpdateMetadata() {
	db->Execute(update_STATIC_data, Serialize(metadata));
}

END_NAMESPACE(Anonymous)


void BlockManager::open_file(const char file[]) {
	db = std::make_unique<Database>(file);

	if (db->ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
		db->Execute(create_STATIC);
		db->Execute(create_OBJECT);
		db->Execute(create_EXPAND);
		db->Execute(create_BUFFER);

		metadata.version = metadata_version;
		metadata.root_index = block_index_invalid;
		metadata.gc_mark = false;
		metadata.gc_phase = GcPhase::Idle;
		metadata.block_count_last_gc = 0;
		db->Execute(insert_STATIC_data, Serialize(metadata));
	} else {
		metadata = Deserialize<Metadata>(db->ExecuteForOne<std::vector<byte>>(select_data_STATIC));
		if (metadata.version != metadata_version) {
			throw std::runtime_error("metadata version doesn't match");
		}
	}
}

void BlockManager::close_file() {
	db.reset();
}

block_ref BlockManager::get_root() {
	return metadata.root_index;
}

void BlockManager::set_root(block_ref index) {
	metadata.root_index = index;
	UpdateMetadata();
}

void BlockManager::collect_garbage() {
	switch (metadata.gc_phase) {
	case GcPhase::Idle: goto idle;
	case GcPhase::Scanning: goto scanning;
	case GcPhase::Sweeping: goto sweeping;
	}
idle:
	if (metadata.root_index == block_index_invalid) {
		if (db->ExecuteForOne<uint64>(select_count_OBJECT)) {
			goto sweep;
		} else {
			return;
		}
	}
	db->Execute(insert_EXPAND_id, metadata.root_index);
	metadata.gc_phase = GcPhase::Scanning;
	UpdateMetadata();
scanning:
	while (db->Execute(insert_BUFFER_limit, gc_buffer_batch_size), db->ExecuteForOne<uint64>(select_count_BUFFER) != 0) {
		db->Execute(delete_EXPAND_limit, gc_buffer_batch_size);
		std::vector<std::vector<index_t>> data_list = db->ExecuteForMultiple<std::vector<index_t>>(select_ref_OBJECT_gc, metadata.gc_mark);
		for (auto& data : data_list) { for (index_t id : data) { db->Execute(insert_EXPAND_id, id); } }
		db->Execute(update_OBJECT_gc, !metadata.gc_mark);
		db->Execute(delete_BUFFER);

		// interrupt
	}
sweep:
	metadata.max_index = db->ExecuteForOne<index_t>(select_max_OBJECT);
	metadata.gc_delete_index = 0;
	metadata.gc_phase = GcPhase::Sweeping;
	UpdateMetadata();
sweeping:
	while (metadata.gc_delete_index <= metadata.max_index) {
		index_t next = db->ExecuteForOne<index_t>(select_next_OBJECT_begin_limit, metadata.gc_delete_index, gc_delete_batch_size) + 1;
		db->Execute(delete_OBJECT_begin_gc_limit, metadata.gc_delete_index, metadata.gc_mark, gc_delete_batch_size);
		metadata.gc_delete_index = next;
		UpdateMetadata();

		// interrupt
	}
	metadata.gc_mark = !metadata.gc_mark;
	metadata.gc_phase = GcPhase::Idle;
	metadata.block_count_last_gc = db->ExecuteForOne<uint64>(select_count_OBJECT);
	UpdateMetadata();
}


std::vector<byte> block<>::read() {
	if (empty()) {
		throw std::runtime_error("cannot read empty block");
	}
	return db->ExecuteForOne<std::vector<byte>>(select_data_OBJECT_id, index);
}

void block<>::write(std::vector<byte> data, std::vector<index_t> ref) {
	if (empty()) {
		index = db->ExecuteForOne<uint64>(insert_id_OBJECT_gc_data_ref, metadata.gc_mark, data, ref);
	} else {
		if (metadata.gc_phase != GcPhase::Scanning) {
			db->Execute(update_OBJECT_data_ref_id, data, ref, index);
		} else {
			bool gc = (bool)db->ExecuteForOne<uint64>(update_gc_OBJECT_data_ref_id, data, ref, index);
			if (gc == !metadata.gc_mark) {
				for (index_t id : ref) {
					db->Execute(insert_EXPAND_id, id);
				}
			}
		}
	}
}


END_NAMESPACE(BlockStore)