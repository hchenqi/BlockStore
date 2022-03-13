#include "file_manager.h"
#include "layout_traits.h"


BEGIN_NAMESPACE(BlockStore)

using namespace Sqlite;


constexpr size_t table_count = 3;

Query select_count_TABLE = "select count(*) from SQLITE_MASTER";  // void -> uint64
Query select_name_TABLE = "select name from SQLITE_MASTER where type = 'table'";  // void -> vector<string>
Query drop_TABLE_name = "drop table ?";  // string -> void

Query init_STATIC = "create table if not exists STATIC (data BLOB)";  // void -> void
Query init_OBJECT = "create table if not exists OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB)";  // void -> void
Query init_EXPAND = "create table if not exists EXPAND (id INTEGER)";  // void -> void

Query select_metadata_STATIC = "select * from STATIC";  // void -> vector<byte>
Query update_STATIC_metadata = "update STATIC set data = ?";  // vector<byte> -> void

Query insert_OBJECT_gc = "insert into OBJECT (gc) values (?)";  // bool -> void
Query select_data_OBJECT = "select data from OBJECT where id = ?";  // uint64 -> vector<byte>
Query update_OBJECT_data = "update OBJECT set data = ? where id = ?";  // vector<byte>, uint64 -> void


FileManager::FileManager(const char file[]) : db(file) {
	if (db.ExecuteForOne<uint64>(select_count_TABLE) != table_count) {
		std::vector<std::string> tables = db.ExecuteForMultiple<std::string>(select_name_TABLE);
		for (auto& table : tables) {
			db.Execute(drop_TABLE_name, table);
		}
		db.Execute(init_STATIC);
		db.Execute(init_OBJECT);
		db.Execute(init_EXPAND);
	}
}

Metadata FileManager::GetMetadata() {

}

void FileManager::SetMetadata(Metadata metadata) {}

data_t FileManager::CreateBlock(std::vector<byte> data) {}

std::pair<byte*, size_t> FileManager::GetBlockData(data_t block_index) {}

void FileManager::SetBlockData(data_t block_index, std::vector<byte> data) {}


END_NAMESPACE(BlockStore)