#pragma once

#include "OrderedRefSet.h"
#include "CppSerialize/stl/string.h"


namespace BlockStore {


template<template<class T> class Cache>
class StringTable : public OrderedRefSet<std::string, Cache> {
private:
	using Base = OrderedRefSet<std::string, Cache>;
	using Tree = Base::Base;
	using NodeCache = Base::NodeCache;
	using LeafCache = Base::LeafCache;
	using KeyCache = Base::KeyCache;

public:
	StringTable(NodeCache& node_cache, LeafCache& leaf_cache, KeyCache& key_cache, block_ref meta) : Base(node_cache, leaf_cache, key_cache, std::move(meta)) {}

public:
	using Tree::clear;

	block<std::string> insert(std::string str) {
		if (auto it = Tree::lower_bound(str); Base::equal(it, str)) {
			return *it;
		} else {
			block<std::string> ref = Base::key_cache.create(std::move(str)).drop();
			Tree::insert(std::move(it), ref);
			return ref;
		}
	}

	block<std::string> insert(block<std::string> ref) {
		block_view<std::string, KeyCache> view = Base::key_cache.read(ref);
		const std::string& str = view.get();
		if (auto it = Tree::lower_bound(str); Base::equal(it, str)) {
			return *it;
		} else {
			Tree::insert(std::move(it), std::move(ref));
			return std::move(view.drop());
		}
	}
};


} // namespace BlockStore
