#pragma once

#include "Tree.h"
#include "../utility/key_ref_comp.h"
#include "../utility/ref_split_control.h"


namespace BlockStore {


template<class Key>
using OrderedRefSetNode = TreeNode<block<Key>>;

template<class Key>
using OrderedRefSetLeaf = TreeLeaf<block<Key>, void>;


template<class Key, template<class T> class Cache>
class OrderedRefSet : public Tree<block<Key>, void, KeyRefComp<Key, Cache>, Cache> {
protected:
	using Base = Tree<block<Key>, void, KeyRefComp<Key, Cache>, Cache>;
	using NodeCache = Base::NodeCache;
	using LeafCache = Base::LeafCache;
	using KeyCache = Cache<Key>;

public:
	OrderedRefSet(NodeCache& node_cache, LeafCache& leaf_cache, KeyCache& key_cache, block_ref meta) : Base(node_cache, leaf_cache, std::move(meta), KeyRefComp<Key, Cache>(key_cache)), key_cache(key_cache) {}

protected:
	KeyCache& key_cache;

public:
	bool contains(const Key& key) const {
		auto it = Base::lower_bound(key);
		return it != Base::end() && key_cache.read(*it).get() == key;
	}

	bool equal(Base::iterator it, const Key& key) const {
		return it != Base::end() && key_cache.read(*it).get() == key;
	}

	void insert(Key key) {
		auto it = Base::lower_bound(key);
		if (equal(it, key)) {
			throw std::invalid_argument("key already exists");
		}
		Base::insert(std::move(it), key_cache.create(std::move(key)).drop());
	}

	using Base::erase;

	void erase(const Key& key) {
		auto it = Base::lower_bound(key);
		if (it == Base::end() || key_cache.read(*it).get() != key) {
			throw std::invalid_argument("key doesn't exist");
		}
		Base::erase(std::move(it));
	}
};


} // namespace BlockStore
