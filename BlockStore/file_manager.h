#pragma once

#include "metadata.h"
#include "block_data.h"

#include <memory>


BEGIN_NAMESPACE(BlockStore)


class Database;

class FileManager {
public:
	FileManager(const char file[]);
	~FileManager();
private:
	std::unique_ptr<Database> db;
	Metadata metadata;
private:
	void MetadataUpdated();
public:
	data_t GetRootIndex() { return metadata.root_index; }
	void SetRootIndex(data_t root_index) { metadata.root_index = root_index; MetadataUpdated(); }
public:
	data_t CreateBlock();
	void SetBlockData(data_t block_index, block_data block_data);
	block_data GetBlockData(data_t block_index);
public:
	void StartGarbageCollection();
};


END_NAMESPACE(BlockStore)