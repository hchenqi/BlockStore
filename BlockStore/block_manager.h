#pragma once

#include "block_ref.h"


BEGIN_NAMESPACE(BlockStore)

class FileManager;
class BlockCache;


class BlockManager {
private:
	friend class block_ref;

public:
	BlockManager(const char file[]);
	~BlockManager();

private:
	std::unique_ptr<FileManager> file;
private:
	data_t CreateBlock();
	void SetBlockData(data_t block_index, block_data block_data);
	block_data GetBlockData(data_t block_index);

private:
	std::unique_ptr<BlockCache> cache;
private:
	bool IsBlockCached(data_t block_index);
	void CacheBlock(data_t block_index, std::shared_ptr<void> block);
	const std::shared_ptr<void>& GetCachedBlock(data_t block_index);
	void SetCachedBlockDirty(data_t block_index);

public:
	block_ref GetRootRef();
	void SetRootRef(data_t root_index);
};


END_NAMESPACE(BlockStore)