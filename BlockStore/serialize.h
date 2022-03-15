#pragma once

#include "layout_traits.h"

#include <vector>


BEGIN_NAMESPACE(BlockStore)

using block_data = std::pair<std::vector<byte>, std::vector<data_t>>;


template<class T>
inline block_data Serialize(const T& object) {
	BlockSizeContext size_context;
	Size(size_context, object);
	std::vector<byte> data(size_context.GetSize());
	std::vector<data_t> index_data(size_context.GetIndexSize());
	BlockSaveContext context(data.data(), data.size(), index_data.data(), index_data.size());
	Save(context, object);
	return { data, index_data };
}


template<class T>
inline T Deserialize(block_data block_data) {
	T object;
	BlockLoadContext context(block_data.first.data(), block_data.first.size(), block_data.second.data(), block_data.second.size());
	Load(context, object);
	return object;
}


END_NAMESPACE(BlockStore)