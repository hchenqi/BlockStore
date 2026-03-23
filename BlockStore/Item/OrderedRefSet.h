#pragma once

#include "Tree.h"


namespace BlockStore {


template<class Key, template<class T> class Cache>
class KeyRefComp {
private:
	using KeyCache = Cache<Key>;
public:
	KeyRefComp(KeyCache& cache) :cache(cache) {}
private:
	KeyCache& cache;
public:
	bool operator()(const block<Key>& ref, const Key& key) const {
		return cache.read(ref).get() < key;
	}
	bool operator()(const Key& key, const block<Key>& ref) const {
		return key < cache.read(ref).get();
	}
};


template<class Key>
using OrderedRefSetNode = TreeNode<block<Key>>;

template<class Key>
using OrderedRefSetLeaf = TreeLeaf<block<Key>, void>;


template<class Key, template<class T> class Cache>
class OrderedRefSet : public Tree<block<Key>, void, KeyRefComp<Key, Cache>, Cache> {
private:
	using Base = Tree<block<Key>, void, KeyRefComp<Key, Cache>, Cache>;
	using NodeCache = Base::NodeCache;
	using LeafCache = Base::LeafCache;
	using KeyCache = Cache<Key>;

public:
	OrderedRefSet(NodeCache& node_cache, LeafCache& leaf_cache, KeyCache& key_cache, block_ref meta) : Base(node_cache, leaf_cache, std::move(meta), KeyRefComp<Key, Cache>(key_cache)), key_cache(key_cache) {}

private:
	KeyCache& key_cache;

public:
	bool contains(const Key& key) {
		auto it = Base::lower_bound(key);
		return it != Base::end() && key_cache.read(*it).get() == key;
	}

	void insert(const Key& key) {
		Base::insert(Base::upper_bound(key), key_cache.create(key).drop());
	}

	void erase_one(const Key& key) {
		auto it = Base::lower_bound(key);
		if (it == Base::end() || key_cache.read(*it).get() != key) {
			throw std::invalid_argument("key doesn't exist");
		}
		Base::erase(std::move(it));
	}
};


} // namespace BlockStore
