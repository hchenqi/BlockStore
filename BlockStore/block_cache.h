#pragma once

#include "core.h"

#include <memory>
#include <unordered_map>
#include <vector>


BEGIN_NAMESPACE(BlockStore)


class BlockCache {
private:
	struct BlockEntry {
		std::shared_ptr<void> block;
		bool dirty;
	};
	std::unordered_map<data_t, BlockEntry> cache;
public:
	bool Has(data_t index) { return cache.find(index) != cache.end(); }
	void Add(data_t index, std::shared_ptr<void> data) { cache.emplace(index, BlockEntry{ std::move(data), false }); }
	const std::shared_ptr<void>& Get(data_t index) { return cache.at(index).block; }
	bool IsDirty(data_t index) { return cache.at(index).dirty; }
	void SetDirty(data_t index) { cache.at(index).dirty = true; }
	void ResetDirty(data_t index) { cache.at(index).dirty = false; }
	void Clear() { cache.clear(); }
};


class NewBlockCache {
private:
	struct Entry {
		std::shared_ptr<void> block;
		data_t file_index = block_index_invalid;
	};
	std::vector<Entry> cache;
public:
	data_t Add(std::shared_ptr<void> ptr) { data_t index = cache.size(); cache.emplace_back().block = std::move(ptr); return index; }
	const std::shared_ptr<void>& Get(data_t index) { return cache[index].block; }
	std::shared_ptr<void> Save(data_t index, data_t file_index) { cache[index].file_index = file_index; return std::move(cache[index].block); }
	bool IsSaved(data_t index) { return cache[index].block == nullptr; }
	data_t GetFileIndex(data_t index) { return cache[index].file_index; }
	void Clear() { cache.clear(); }
};


END_NAMESPACE(BlockStore)