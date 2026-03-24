#pragma once

#include "../Item/Tree.h"


namespace BlockStore {


template<>
struct TreeSplitControl<block_ref, void> {
	static bool node_should_split(const auto& keys) {
		return sizeof(ref_t) + sizeof(size_t) + keys.size() * 2 * sizeof(ref_t) > block_size_limit;
	}
	static bool leaf_should_split(const auto& leaf) {
		return sizeof(size_t) + leaf.size() * sizeof(ref_t) > block_size_limit;
	}
};

template<class Key>
struct TreeSplitControl<block<Key>, void> : TreeSplitControl<block_ref, void> {};


template<>
struct TreeSplitControl<block_ref, block_ref> : TreeSplitControl<block_ref, void> {
	static bool leaf_should_split(const auto& leaf) {
		return sizeof(size_t) + leaf.size() * 2 * sizeof(ref_t) > block_size_limit;
	}
};

template<class Key>
struct TreeSplitControl<block<Key>, block_ref> : TreeSplitControl<block_ref, block_ref> {};

template<class Value>
struct TreeSplitControl<block_ref, block<Value>> : TreeSplitControl<block_ref, block_ref> {};

template<class Key, class Value>
struct TreeSplitControl<block<Key>, block<Value>> : TreeSplitControl<block_ref, block_ref> {};


} // namespace BlockStore
