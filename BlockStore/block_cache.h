#pragma once

#include "core.h"

#include <memory>
#include <unordered_map>


BEGIN_NAMESPACE(BlockStore)


class BlockCache {
private:
	struct BlockEntry {
		size_t ref_count;
		bool dirty;
		std::shared_ptr<void> block;
	};
	std::unordered_map<data_t, BlockEntry> cache;
public:
	bool Has(data_t index) { return cache.find(index) != cache.end(); }
	void Add(data_t index, std::shared_ptr<void> data) { cache.emplace(index, BlockEntry{ 0, false, std::move(data) }); }
	const std::shared_ptr<void>& Get(data_t index) { return cache.at(index).block; }
	void SetDirty(data_t index) { cache.at(index).dirty = true; }
};


END_NAMESPACE(BlockStore)