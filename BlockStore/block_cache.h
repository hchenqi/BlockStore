#pragma once

#include "core.h"

#include <memory>
#include <unordered_map>


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


END_NAMESPACE(BlockStore)