#include "file_manager.h"


BEGIN_NAMESPACE(BlockStore)

using namespace Sqlite;


Query init_STATIC = "create table if not exists STATIC (data BLOB)";
Query init_OBJECT = "create table if not exists OBJECT (id INTEGER primary key, gc BOOLEAN, data BLOB)";
Query init_EXPAND = "create table if not exists EXPAND (id INTEGER primary key)";

Query select_STATIC_metadata = "select * from STATIC";
Query update_STATIC_metadata = "";

Query insert_OBJECT_block = "insert into OBJECT (gc, data) values (?, ?)";
Query select_OBJECT_data = "select data from OBJECT where id = ?";
Query update_OBJECT_data = "update OBJECT set data = ? where id = ?";




END_NAMESPACE(BlockStore)