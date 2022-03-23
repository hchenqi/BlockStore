#include "block_ref.h"
#include "block_manager.h"
#include "file_manager.h"
#include "block_cache.h"
#include "block_cache_new.h"


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Anonymous)

std::unique_ptr<FileManager> file;
BlockCache block_cache;
NewBlockCache new_block_cache;

END_NAMESPACE(Anonymous)


void BlockManager::open(const char path[]) { if (file != nullptr) { throw std::invalid_argument("file already open"); } file.reset(new FileManager(path)); }
block_ref<> BlockManager::get_root() { return file->GetRootIndex(); }
void BlockManager::set_root(const block_ref<>& root) { file->SetRootIndex(root.index); }
void BlockManager::clear_cache() { block_cache.Clear(); new_block_cache.Clear(); }
void BlockManager::close() { file.reset(); }


void block_ref<>::CacheNewBlock(std::shared_ptr<void> block) { index = new_block_cache.Add(std::move(block)); }
const std::shared_ptr<void>& block_ref<>::GetCachedNewBlock() { return new_block_cache.Get(index); }
bool block_ref<>::IsNewBlockSaved() { return new_block_cache.IsSaved(index) ? *this = block_ref(new_block_cache.GetFileIndex(index)), true : false; }

void block_ref<>::CreateBlock() {
	data_t file_index = file->CreateBlock();
	std::shared_ptr<void> block = new_block_cache.Save(index, file_index);
	*this = block_ref(file_index);
	CacheBlock(std::move(block));
	SetCachedBlockDirty();
}

void block_ref<>::SetBlockData(block_data block_data) { file->SetBlockData(index, block_data); }
block_data block_ref<>::GetBlockData() { return file->GetBlockData(index); }

bool block_ref<>::IsBlockCached() const { return block_cache.Has(index); }
void block_ref<>::CacheBlock(std::shared_ptr<void> block) { block_cache.Add(index, std::move(block)); }
const std::shared_ptr<void>& block_ref<>::GetCachedBlock() { return block_cache.Get(index); }
bool block_ref<>::IsCachedBlockDirty() { return block_cache.IsDirty(index); }
void block_ref<>::SetCachedBlockDirty() { block_cache.SetDirty(index); }
void block_ref<>::ResetCachedBlockDirty() { block_cache.ResetDirty(index); }


END_NAMESPACE(BlockStore)