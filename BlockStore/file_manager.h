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
	Metadata& GetMetadata() { return metadata; }
	void MetadataUpdated();
public:
	data_t CreateBlock();
	void SetBlockData(data_t block_index, block_data block_data);
	block_data GetBlockData(data_t block_index);
public:
	void StartGC();
};


END_NAMESPACE(BlockStore)