#pragma once

#include "metadata.h"
#include "sqlite_helper.h"

#include <vector>


BEGIN_NAMESPACE(BlockStore)


class FileManager {
public:
	FileManager(const char file[]) : database(file) {}
private:
	Sqlite::Database database;
public:
	Metadata GetMetadata() {}
	void SetMetadata(Metadata metadata) {}
public:
	data_t CreateBlock(std::vector<byte> data) {}
	std::pair<byte*, size_t> GetBlockData(data_t block_index) {}
	void SetBlockData(data_t block_index, std::vector<byte> data) {}
public:

};


END_NAMESPACE(BlockStore)