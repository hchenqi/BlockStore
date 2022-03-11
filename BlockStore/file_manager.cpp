#include "file_manager.h"
#include "layout_traits.h"


BEGIN_NAMESPACE(BlockStore)

using namespace Sqlite;


Query init_STATIC = "create table if not exists STATIC (data BLOB)";
Query init_OBJECT = "create table if not exists OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB)";
Query init_EXPAND = "create table if not exists EXPAND (id INTEGER)";

Query select_STATIC_metadata = "select * from STATIC";  // void -> vector<byte>
Query update_STATIC_metadata = "update STATIC set data = ?";  // vector<byte> -> void

Query insert_OBJECT_block = "insert into OBJECT (gc, data) values (?, ?)";  // bool, vector<byte> -> void
Query select_OBJECT_data = "select data from OBJECT where id = ?";  // data_t -> vector<byte>
Query update_OBJECT_data = "update OBJECT set data = ? where id = ?";  // data_t, vector<byte> -> void


FileManager::FileManager(const char file[]) : db(file) {
	db.Execute(init_STATIC);
	db.Execute(init_OBJECT);
	db.Execute(init_EXPAND);
}

Metadata FileManager::GetMetadata() {
	db.Execute<Metadata>()
}

void FileManager::SetMetadata(Metadata metadata) {}

data_t FileManager::CreateBlock(std::vector<byte> data) {}

std::pair<byte*, size_t> FileManager::GetBlockData(data_t block_index) {}

void FileManager::SetBlockData(data_t block_index, std::vector<byte> data) {}


END_NAMESPACE(BlockStore)