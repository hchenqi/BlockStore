#pragma once

#include "Tree.h"
#include "../utility/key_ref_comp.h"
#include "../utility/ref_split_control.h"


namespace BlockStore {


template<class Key>
using OrderedRefMapNode = TreeNode<block<Key>>;

template<class Key, class Value>
using OrderedRefMapLeaf = TreeLeaf<block<Key>, block<Value>>;


template<class Key, class Value, template<class T> class Cache>
class OrderedRefMap : public Tree<block<Key>, block<Value>, KeyRefComp<Key, Cache>, Cache> {
private:
	using Base = Tree<block<Key>, block<Value>, KeyRefComp<Key, Cache>, Cache>;
	using NodeCache = Base::NodeCache;
	using LeafCache = Base::LeafCache;
	using KeyCache = Cache<Key>;
	using ValueCache = Cache<Value>;

public:
	OrderedRefMap(NodeCache& node_cache, LeafCache& leaf_cache, KeyCache& key_cache, ValueCache& value_cache, block_ref meta) : Base(node_cache, leaf_cache, std::move(meta), KeyRefComp<Key, Cache>(key_cache)), key_cache(key_cache), value_cache(value_cache) {}

private:
	KeyCache& key_cache;
	ValueCache& value_cache;

public:
	bool contains(const Key& key) const {
		auto it = Base::lower_bound(key);
		return it != Base::end() && key_cache.read((*it).first).get() == key;
	}

	bool equal(Base::iterator it, const Key& key) const {
		return it != Base::end() && key_cache.read((*it).first).get() == key;
	}

	void insert(Key key, Value value) {
		auto it = Base::lower_bound(key);
		if (equal(it, key)) {
			throw std::invalid_argument("key already exists");
		}
		Base::insert(std::move(it), std::make_pair(key_cache.create(std::move(key)).drop(), value_cache.create(std::move(value)).drop()));
	}

	using Base::erase;

	void erase(const Key& key) {
		auto it = Base::lower_bound(key);
		if (it == Base::end() || key_cache.read((*it).first).get() != key) {
			throw std::invalid_argument("key doesn't exist");
		}
		Base::erase(std::move(it));
	}

	block_view<Value, ValueCache> at(const Key& key) {
		auto it = Base::lower_bound(key);
		if (it == Base::end() || key_cache.read((*it).first).get() != key) {
			throw std::invalid_argument("key doesn't exist");
		}
		return value_cache.read((*it).second);
	}
};


} // namespace BlockStore
