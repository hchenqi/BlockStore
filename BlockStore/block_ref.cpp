#include "block_ref.h"
#include "block_manager.h"
#include "block_cache_new.h"


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Anonymous)

NewBlockCache new_block_cache;

END_NAMESPACE(Anonymous)


void block_ref::CacheNewBlock(std::shared_ptr<void> block) { index = new_block_cache.Add(std::move(block)); }
const std::shared_ptr<void>& block_ref::GetCachedNewBlock() { return new_block_cache.Get(index); }

block_data block_ref::GetBlockData(data_t block_index) { return manager->GetBlockData(index); }

bool block_ref::IsBlockCached() const { return manager->IsBlockCached(index); }
void block_ref::CacheBlock(std::shared_ptr<void> block) { manager->CacheBlock(index, std::move(block)); }
const std::shared_ptr<void>& block_ref::GetCachedBlock() { return manager->GetCachedBlock(index); }
void block_ref::SetCachedBlockDirty() { manager->SetCachedBlockDirty(index); }


END_NAMESPACE(BlockStore)