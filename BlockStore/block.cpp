#include "block_manager.h"

#include "Sqlite3/sqlite_helper.h"

#include <memory>


#pragma comment(lib, "Sqlite3.lib")


BEGIN_NAMESPACE(CppSerialize)

template<>
constexpr bool has_trivial_layout<BlockStore::block_ref> = true;

END_NAMESPACE(CppSerialize)


BEGIN_NAMESPACE(BlockStore)

enum GcPhase { Idle, Scan, Sweep };

struct Metadata {
	index_t root_index;
	bool gc_mark;
	GcPhase gc_phase;
};

using block_data = std::tuple<std::vector<byte>, std::vector<byte>>;

BEGIN_NAMESPACE(Anonymous)

using namespace SqliteHelper;

std::unique_ptr<Database> db;
Metadata metadata;

constexpr size_t table_count = 4;

Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> uint64

Query create_STATIC = "create table STATIC (data BLOB)";  // void -> void
Query create_OBJECT = "create table OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB, ref BLOB)";  // void -> void
Query create_EXPAND = "create table EXPAND (id INTEGER)";  // void -> void
Query create_BUFFER = "create table BUFFER (id INTEGER)";  // void -> void

Query insert_STATIC_data = "insert into STATIC values (?)";  // vector<byte> -> void
Query select_data_STATIC = "select * from STATIC";  // void -> vector<byte>
Query update_STATIC_data = "update STATIC set data = ?";  // vector<byte> -> void

Query insert_id_OBJECT_gc = "insert into OBJECT (gc) values (?) returning id";  // bool -> uint64
Query select_count_OBJECT_id_gc = "select count(*) from OBJECT where id = ? and gc = ?";  // uint64, bool -> uint64
Query select_data_ref_OBJECT_id = "select data, ref from OBJECT where id = ?";  // uint64 -> vector<byte>, vector<byte>
Query update_OBJECT_data_ref_id = "update OBJECT set data = ?, ref = ? where id = ?";  // vector<byte>, vector<byte>, uint64 -> void

Query insert_BUFFER = "insert into BUFFER select * from EXPAND order by rowid desc limit 16";  // void -> void
Query select_count_BUFFER = "select count(*) from BUFFER";  // void -> uint64
Query delete_EXPAND = "delete from EXPAND where rowid in (select rowid from EXPAND order by rowid desc limit 16)";  // void -> void
Query select_ref_OBJECT_gc = "select ref from OBJECT where id in (select * from BUFFER) and gc = ?";  // uint64 -> vector<vector<byte>>
Query insert_EXPAND_id = "insert into EXPAND values (?)";  // uint64 -> void
Query update_OBJECT_gc = "update OBJECT set gc = ? where id in (select * from BUFFER)";  // bool -> void
Query delete_BUFFER = "delete from BUFFER";  // void -> void
Query delete_OBJECT_gc = "delete from OBJECT where gc = ?";  // bool -> void

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
		metadata.root_index = block_index_invalid;
		metadata.gc_mark = false;
		metadata.gc_phase = GcPhase::Idle;
		db->Execute(insert_STATIC_data, Serialize(metadata));
	} else {
		metadata = Deserialize<Metadata>(db->ExecuteForOne<std::vector<byte>>(select_data_STATIC));
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
	case GcPhase::Scan: goto scan;
	case GcPhase::Sweep: goto sweep;
	}
idle:
	db->Execute(insert_EXPAND_id, metadata.root_index);
	metadata.gc_phase = GcPhase::Scan;
	UpdateMetadata();
scan:
	while (db->Execute(insert_BUFFER), db->ExecuteForOne<uint64>(select_count_BUFFER) != 0) {
		db->Execute(delete_EXPAND);
		std::vector<std::vector<byte>> data_list = db->ExecuteForMultiple<std::vector<byte>>(select_ref_OBJECT_gc, metadata.gc_mark);
		for (auto& data : data_list) { for (index_t id : Deserialize<std::vector<block_ref>>(data)) { db->Execute(insert_EXPAND_id, id); } }
		db->Execute(update_OBJECT_gc, !metadata.gc_mark);
		db->Execute(delete_BUFFER);
	}
	metadata.gc_phase = GcPhase::Sweep;
	UpdateMetadata();
sweep:
	db->Execute(delete_OBJECT_gc, metadata.gc_mark);
	metadata.gc_mark = !metadata.gc_mark;
	metadata.gc_phase = GcPhase::Idle;
	UpdateMetadata();
}


std::pair<std::vector<byte>, std::vector<block_ref>> block::read() {
	if (empty()) {
		return {};
	}
	block_data data = db->ExecuteForOne<block_data>(select_data_ref_OBJECT_id, index);
	return { std::get<0>(data), Deserialize<std::vector<block_ref>>(std::get<1>(data)) };
}

void block::write(std::vector<byte> data, std::vector<block_ref> ref_list) {
	if (empty()) {
		index = db->ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc_mark);
	}
	db->Execute(update_OBJECT_data_ref_id, data, Serialize(ref_list), index);
	if (metadata.gc_phase == GcPhase::Scan && db->ExecuteForOne<uint64>(select_count_OBJECT_id_gc, index, !metadata.gc_mark) != 0) {
		for (index_t id : ref_list) { db->Execute(insert_EXPAND_id, id); }
	}
}


END_NAMESPACE(BlockStore)