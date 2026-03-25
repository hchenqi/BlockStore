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
public:
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

	std::optional<block_view<Key, KeyCache>> compare(Base::iterator it, const Key& key) const {
		if (it == Base::end()) {
			return std::nullopt;
		}
		block_view<Key, KeyCache> view = key_cache.read(*it);
		if (view.get() == key) {
			return std::move(view);
		} else {
			return std::nullopt;
		}
	}

	block_view<Key, KeyCache> insert(Key key) {
		auto it = Base::lower_bound(key);
		if (auto ret = compare(it, key); ret != std::nullopt) {
			return std::move(ret.value());
		} else {
			block_view<Key, KeyCache> view = key_cache.create(std::move(key));
			Base::insert(std::move(it), view);
			return view;
		}
	}

	block_view<Key, KeyCache> insert(block<Key> ref) {
		block_view<Key, KeyCache> view = key_cache.read(ref);
		const Key& key = view.get();
		auto it = Base::lower_bound(key);
		if (auto ret = compare(it, key); ret != std::nullopt) {
			return std::move(ret.value());
		} else {
			Base::insert(std::move(it), std::move(ref));
			return view;
		}
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
