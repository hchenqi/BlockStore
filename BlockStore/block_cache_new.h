#pragma once

#include "core.h"

#include <memory>
#include <vector>


BEGIN_NAMESPACE(BlockStore)


class NewBlockCache {
private:
	struct BlockEntry {
		union {
			data_t next_index = block_index_invalid;  // as unused entry
			data_t file_index;  // as saved block
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
		data_t index = entry.next_index; entry.block = std::move(ptr);
		return index;
	}
	const std::shared_ptr<void>& Get(data_t index) {
		return cache[index].block;
	}
	std::shared_ptr<void> Save(data_t index, data_t file_index) {
		cache[index].file_index = file_index;
		return std::move(cache[index].block);
	}
	bool IsSaved(data_t index) {
		return cache[index].block == nullptr;
	}
	data_t GetFileIndex(data_t index) {
		return cache[index].file_index;
	}
	void Clear() {
		cache.clear(); next_index = block_index_invalid;
	}
};


END_NAMESPACE(BlockStore)