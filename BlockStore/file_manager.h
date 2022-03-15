#pragma once

#include "metadata.h"
#include "sqlite_helper.h"
#include "serialize.h"


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
	block_data GetBlockData(data_t block_index);
	void SetBlockData(data_t block_index, block_data block_data);
public:
	void StartGC();
};


END_NAMESPACE(BlockStore)