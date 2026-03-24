#pragma once

#include "Tree.h"
#include "../utility/ref_split_control.h"


namespace BlockStore {


using UnorderedRefSetNode = TreeNode<block_ref>;
using UnorderedRefSetLeaf = TreeLeaf<block_ref, void>;


template<template<class T> class Cache>
class UnorderedRefSet : public Tree<block_ref, void, std::less<block_ref>, Cache> {
private:
	using Base = Tree<block_ref, void, std::less<block_ref>, Cache>;
	using NodeCache = Base::NodeCache;
	using LeafCache = Base::LeafCache;

public:
	UnorderedRefSet(NodeCache& node_cache, LeafCache& leaf_cache, block_ref meta) : Base(node_cache, leaf_cache, std::move(meta), std::less<block_ref>()) {}

public:
	bool contains(const block_ref& ref) const {
		auto it = Base::lower_bound(ref);
		return it != Base::end() && *it == ref;
	}

	bool equal(Base::iterator it, const block_ref& ref) const {
		return it != Base::end() && *it == ref;
	}

	void insert(block_ref ref) {
		auto it = Base::lower_bound(ref);
		if (equal(it, ref)) {
			throw std::invalid_argument("ref already exists");
		}
		Base::insert(std::move(it), std::move(ref));
	}

	using Base::erase;

	void erase(const block_ref& ref) {
		auto it = Base::lower_bound(ref);
		if (it == Base::end() || *it != ref) {
			throw std::invalid_argument("ref doesn't exist");
		}
		Base::erase(std::move(it));
	}
};


} // namespace BlockStore
