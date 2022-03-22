#pragma once

#include "metadata.h"
#include "sqlite_helper.h"
#include "block_data.h"


BEGIN_NAMESPACE(BlockStore)


class FileManager {
public:
	FileManager(const char file[]);
private:
	Sqlite::Database db;
	Metadata metadata;
public:
	data_t GetRootIndex() { return metadata.root_index; }
	void SetRootIndex(data_t root_index) { metadata.root_index = root_index; MetadataUpdated(); }
	void MetadataUpdated();
public:
	data_t CreateBlock();
	void SetBlockData(data_t block_index, block_data block_data);
	block_data GetBlockData(data_t block_index);
public:
	void StartGarbageCollection();
};


END_NAMESPACE(BlockStore)