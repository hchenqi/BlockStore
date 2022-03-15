#include "file_manager.h"
#include "layout_traits.h"


BEGIN_NAMESPACE(BlockStore)

using namespace Sqlite;


constexpr size_t table_count = 3;

Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> uint64

Query create_STATIC = "create table if not exists STATIC (data BLOB)";  // void -> void
Query create_OBJECT = "create table if not exists OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB)";  // void -> void
Query create_EXPAND = "create table if not exists EXPAND (id INTEGER)";  // void -> void
Query create_BUFFER = "create table if not exists BUFFER (id INTEGER)";  // void -> void

Query insert_STATIC_data = "insert into STATIC values (?)";  // vector<byte> -> void
Query select_data_STATIC = "select * from STATIC";  // void -> vector<byte>
Query update_STATIC_data = "update STATIC set data = ?";  // vector<byte> -> void

Query insert_id_OBJECT_gc = "insert into OBJECT (gc) values (?) returning id";  // bool -> uint64
Query select_data_OBJECT_id = "select data from OBJECT where id = ?";  // uint64 -> vector<byte>
Query update_OBJECT_data_id = "update OBJECT set data = ? where id = ?";  // vector<byte>, uint64 -> void

Query select_count_BUFFER = "insert into BUFFER (select * from EXPAND order by rowid desc limit 16) returning count(*)";  // void -> uint64
Query delete_EXPAND = "delete from EXPAND where rowid in (select rowid from EXPAND order by rowid desc limit 16)";  // void -> void
Query select_data_OBJECT_gc = "select data from OBJECT where id in (select * from BUFFER) and gc = ?";  // bool -> vector<vector<byte>>
Query insert_EXPAND_id = "insert into EXPAND values (?)";  // uint64 -> void
Query update_OBJECT_gc = "update OBJECT set gc = ? where id in (select * from BUFFER)";  // bool -> void
Query delete_BUFFER = "delete from BUFFER";  // void -> void
Query delete_OBJECT_gc = "delete from OBJECT where gc = ?";  // bool -> void


FileManager::FileManager(const char file[]) : db(file) {
	if (db.ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
		db.Execute(create_STATIC);
		db.Execute(create_OBJECT);
		db.Execute(create_EXPAND);
		db.Execute(create_BUFFER);
		metadata.root_block_index = CreateBlock();
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

std::vector<byte> FileManager::GetBlockData(data_t block_index) {
	return db.ExecuteForOne<std::vector<byte>>(select_data_OBJECT_id, block_index);
}

void FileManager::SetBlockData(data_t block_index, std::vector<byte> data) {
	db.Execute(update_OBJECT_data_id, data, block_index);
}

void FileManager::StartGC() {
	metadata.gc_phase = GcPhase::Scan;
	while (db.ExecuteForOne<uint64>(select_count_BUFFER)) {
		db.Execute(delete_EXPAND);
		std::vector<std::vector<byte>> data_list = db.ExecuteForMultiple<std::vector<byte>>(select_data_OBJECT_gc, metadata.gc_mark);
		for (auto& data : data_list) {
			for (auto& child_id : GetChildIdList(data)) {
				db.Execute(insert_EXPAND_id, child_id);
			}
		}
		db.Execute(update_OBJECT_gc, !metadata.gc_mark);
		db.Execute(delete_BUFFER);
	}
	metadata.gc_phase = GcPhase::Sweep;
	db.Execute(delete_OBJECT_gc, metadata.gc_mark);
	metadata.gc_mark = !metadata.gc_mark;
	metadata.gc_phase = GcPhase::Idle;
}


END_NAMESPACE(BlockStore)