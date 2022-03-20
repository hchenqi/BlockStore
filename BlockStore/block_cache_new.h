#pragma once

#include "core.h"

#include <memory>
#include <vector>


BEGIN_NAMESPACE(BlockStore)


class NewBlockCache {
private:
	struct BlockEntry {
		union {
			data_t ref_count;
			data_t next_index = block_index_invalid;
			data_t const_block_index;
		};
		std::shared_ptr<void> block;
	};
	std::vector<BlockEntry> cache;
	data_t next_index = block_index_invalid;
private:
	BlockEntry& AllocateEntry() {
		if (next_index == block_index_invalid) { next_index = cache.size(); cache.emplace_back(); }
		BlockEntry& entry = cache[next_index]; std::swap(next_index, entry.next_index); return entry;
	}
	void DeallocateEntry(data_t index) {
		BlockEntry& entry = cache[index]; entry.block.reset();
		entry.next_index = next_index; next_index = index;
	}
public:
	data_t Add(std::shared_ptr<void> ptr) {
		BlockEntry& entry = AllocateEntry();
		data_t index = entry.next_index; entry.block = std::move(ptr); entry.ref_count = 1;
		return index;
	}
	const std::shared_ptr<void>& Get(data_t index) {
		return cache[index].block;
	}


	void IncRefNewBlock(data_t index) {
		++cache[index].ref_count;
	}
	void DecRefNewBlock(data_t index) {
		if (--cache[index].ref_count == 0) { DeallocateEntry(index); }
	}
	bool IsNewBlockSaved(data_t index) {
		return cache[index].block == nullptr;
	}
	void SaveNewBlock(data_t index, data_t block_index) {
		cache[index].const_block_index = block_index;
		cache[index].block.reset();
	}
	data_t GetSavedBlockIndex(data_t index) {
		return cache[index].const_block_index;
	}
	void ClearNewBlock() {
		cache.clear(); next_index = block_index_invalid;
	}
};


END_NAMESPACE(BlockStore)