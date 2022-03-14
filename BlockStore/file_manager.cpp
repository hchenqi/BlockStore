#include "file_manager.h"
#include "layout_traits.h"


BEGIN_NAMESPACE(BlockStore)

using namespace Sqlite;


constexpr size_t table_count = 3;

Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> uint64

Query init_STATIC = "create table if not exists STATIC (data BLOB)";  // void -> void
Query init_OBJECT = "create table if not exists OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB)";  // void -> void
Query init_EXPAND = "create table if not exists EXPAND (id INTEGER primary key)";  // void -> void

Query insert_STATIC_data = "insert into STATIC values (?)";  // vector<byte> -> void
Query select_data_STATIC = "select * from STATIC";  // void -> vector<byte>
Query update_STATIC_data = "update STATIC set data = ?";  // vector<byte> -> void

Query insert_OBJECT_gc = "insert into OBJECT (gc) values (?)";  // bool -> void
Query select_data_OBJECT = "select data from OBJECT where id = ?";  // uint64 -> vector<byte>
Query update_OBJECT_data = "update OBJECT set data = ? where id = ?";  // vector<byte>, uint64 -> void

Query insert_EXPAND_id = "insert into EXPAND values (?)";  // uint64 -> void
Query select_EXPAND_id = "select * from EXPAND desc limit 16";  // void -> vector<uint64>
Query delete_EXPAND_id = "delete from EXPAND where id = ?";  // uint64 -> void
Query select_OBJECT_gc = "select data from OBJECT where id = ? and gc = ?";  // uint64, bool -> optional<vector<byte>>
Query update_OBJECT_gc = "update OBJECT set gc = ? where id = ?";  // bool, uint64 -> void
Query delete_OBJECT_gc = "delete from OBJECT where gc = ?";  // bool -> void


FileManager::FileManager(const char file[]) : db(file) {
	if (db.ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
		db.Execute(init_STATIC);
		db.Execute(init_OBJECT);
		db.Execute(init_EXPAND);
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
	db.Execute(insert_OBJECT_gc, metadata.gc_mark);
	return db.GetLastInsertID();
}

std::vector<byte> FileManager::GetBlockData(data_t block_index) {
	return db.ExecuteForOne<std::vector<byte>>(select_data_OBJECT, block_index);
}

void FileManager::SetBlockData(data_t block_index, std::vector<byte> data) {
	db.Execute(update_OBJECT_data, data, block_index);
}

void FileManager::StartGC() {
	metadata.gc_phase = GcPhase::Scan;
	std::vector<uint64> batch;
	while (!(batch = db.ExecuteForMultiple<uint64>(select_EXPAND_id)).empty()) {
		for (auto id : batch) {
			if (auto data = db.ExecuteForOneOrNone<std::vector<byte>>(select_OBJECT_gc, id, metadata.gc_mark); data.has_value()) {
				std::vector<uint64> child_id_list = GetChildIdList(data.value());
				for (auto child_id : child_id_list) {
					db.Execute(insert_EXPAND_id, child_id);
				}
				db.Execute(update_OBJECT_gc, id, !metadata.gc_mark);
				db.Execute(delete_EXPAND_id, id);
			}
		}
	}
	metadata.gc_phase = GcPhase::Sweep;
	db.Execute(delete_OBJECT_gc, metadata.gc_mark);
	metadata.gc_mark = !metadata.gc_mark;
	metadata.gc_phase = GcPhase::Idle;
}


END_NAMESPACE(BlockStore)