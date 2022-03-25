#include "file_manager.h"
#include "trivial_serializer.h"


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Anonymous)

using namespace Sqlite;

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
Query select_data_ref_OBJECT_id = "select data, ref from OBJECT where id = ?";  // uint64 -> vector<byte>, vector<data_t>
Query update_OBJECT_data_ref_id = "update OBJECT set data = ?, ref = ? where id = ?";  // vector<byte>, vector<data_t>, uint64 -> void

Query insert_BUFFER = "insert into BUFFER select * from EXPAND order by rowid desc limit 16";  // void -> void
Query select_count_BUFFER = "select count(*) from BUFFER";  // void -> uint64
Query delete_EXPAND = "delete from EXPAND where rowid in (select rowid from EXPAND order by rowid desc limit 16)";  // void -> void
Query select_ref_OBJECT_gc = "select ref from OBJECT where id in (select * from BUFFER) and gc = ?";  // bool -> vector<vector<data_t>>
Query insert_EXPAND_id = "insert into EXPAND values (?)";  // uint64 -> void
Query update_OBJECT_gc = "update OBJECT set gc = ? where id in (select * from BUFFER)";  // bool -> void
Query delete_BUFFER = "delete from BUFFER";  // void -> void
Query delete_OBJECT_gc = "delete from OBJECT where gc = ?";  // bool -> void

END_NAMESPACE(Anonymous)


FileManager::FileManager(const char file[]) : db(file) {
	if (db.ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
		db.Execute(create_STATIC);
		db.Execute(create_OBJECT);
		db.Execute(create_EXPAND);
		db.Execute(create_BUFFER);
		metadata.root_index = block_index_invalid;
		metadata.gc_mark = false;
		metadata.gc_phase = GcPhase::Idle;
		db.Execute(insert_STATIC_data, Serialize(metadata));
	} else {
		metadata = Deserialize<Metadata>(db.ExecuteForOne<std::vector<byte>>(select_data_STATIC));
	}
}

void FileManager::MetadataUpdated() {
	db.Execute(update_STATIC_data, Serialize(metadata));
}

data_t FileManager::CreateBlock() {
	return db.ExecuteForOne<uint64>(insert_id_OBJECT_gc, metadata.gc_mark);
}

void FileManager::SetBlockData(data_t block_index, block_data block_data) {
	db.Execute(update_OBJECT_data_ref_id, block_data.first, block_data.second, block_index);
	if (metadata.gc_phase == GcPhase::Scan && db.ExecuteForOne<data_t>(select_count_OBJECT_id_gc, block_index, !metadata.gc_mark) != 0) {
		for (auto& id : block_data.second) { db.Execute(insert_EXPAND_id, id); }
	}
}

block_data FileManager::GetBlockData(data_t block_index) {
	return db.ExecuteForOne<block_data>(select_data_ref_OBJECT_id, block_index);
}

void FileManager::StartGarbageCollection() {
	switch (metadata.gc_phase) {
	case GcPhase::Idle: goto idle;
	case GcPhase::Scan: goto scan;
	case GcPhase::Sweep: goto sweep;
	}
idle:
	db.Execute(insert_EXPAND_id, metadata.root_index);
	metadata.gc_phase = GcPhase::Scan; MetadataUpdated();
scan:
	while (db.Execute(insert_BUFFER), db.ExecuteForOne<uint64>(select_count_BUFFER) != 0) {
		db.Execute(delete_EXPAND);
		std::vector<std::vector<data_t>> data_list = db.ExecuteForMultiple<std::vector<data_t>>(select_ref_OBJECT_gc, metadata.gc_mark);
		for (auto& data : data_list) { for (auto& id : data) { db.Execute(insert_EXPAND_id, id); } }
		db.Execute(update_OBJECT_gc, !metadata.gc_mark);
		db.Execute(delete_BUFFER);
	}
	metadata.gc_phase = GcPhase::Sweep; MetadataUpdated();
sweep:
	db.Execute(delete_OBJECT_gc, metadata.gc_mark);
	metadata.gc_mark = !metadata.gc_mark;
	metadata.gc_phase = GcPhase::Idle; MetadataUpdated();
}


END_NAMESPACE(BlockStore)