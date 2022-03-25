#include "block_ref.h"
#include "block_manager.h"
#include "file_manager.h"

#include <memory>


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Anonymous)

std::unique_ptr<FileManager> file;

END_NAMESPACE(Anonymous)


void BlockManager::open(const char path[]) { file.reset(new FileManager(path)); }
block_ref BlockManager::get_root() { return file->GetRootIndex(); }
void BlockManager::set_root(const block_ref& root) { file->SetRootIndex(root.index); }
void BlockManager::start_gc() { file->StartGarbageCollection(); }
void BlockManager::close() { file.reset(); }


void block_ref::CreateBlock() { index = file->CreateBlock(); }
void block_ref::SetBlockData(block_data block_data) { file->SetBlockData(index, std::move(block_data)); }
block_data block_ref::GetBlockData() { return file->GetBlockData(index); }


END_NAMESPACE(BlockStore)