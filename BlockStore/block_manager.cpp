#include "block_manager.h"
#include "file_manager.h"
#include "block_cache.h"


BEGIN_NAMESPACE(BlockStore)


BlockManager::BlockManager(const char file[]) : file(new FileManager(file)), cache(new BlockCache) {}
BlockManager::~BlockManager() {}

data_t BlockManager::CreateBlock() { return file->CreateBlock(); }
void BlockManager::SetBlockData(data_t block_index, block_data block_data) { return file->SetBlockData(block_index, std::move(block_data)); }
block_data BlockManager::GetBlockData(data_t block_index) { return file->GetBlockData(block_index); }

bool BlockManager::IsBlockCached(data_t block_index) { return cache->Has(block_index); }
void BlockManager::CacheBlock(data_t block_index, std::shared_ptr<void> block) { cache->Add(block_index, std::move(block)); }
const std::shared_ptr<void>& BlockManager::GetCachedBlock(data_t block_index) { return cache->Get(block_index); }
void BlockManager::SetCachedBlockDirty(data_t block_index) { cache->SetDirty(block_index); }


END_NAMESPACE(BlockStore)