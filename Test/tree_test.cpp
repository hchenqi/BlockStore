#include "BlockStore/Item/Tree.h"
#include "CppSerialize/stl/string.h"
#include "common.h"


using namespace BlockStore;


template<class Key, class KeyCache>
class KeyRefComp {
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


template<class Key, class NodeCache, class LeafCache, class KeyCache>
class OrderedRefSet : public Tree<block<Key>, void, NodeCache, LeafCache, KeyRefComp<Key, KeyCache>> {
private:
	using Base = Tree<block<Key>, void, NodeCache, LeafCache, KeyRefComp<Key, KeyCache>>;
public:
	OrderedRefSet(NodeCache& node_cache, LeafCache& leaf_cache, KeyCache& key_cache, block_ref meta) : Base(node_cache, leaf_cache, std::move(meta), KeyRefComp<Key, KeyCache>(key_cache)), key_cache(key_cache) {}
private:
	KeyCache& key_cache;
public:
	void insert(const Key& key) {
		Base::insert(key, key_cache.create(key));
	}
};


int main() {
	BlockManager block_manager("tree_test.db");
	BlockCacheDynamic cache(block_manager);

	OrderedRefSet<std::string, BlockCacheDynamicAdapter<OrderedRefSetNode<std::string>>, BlockCacheDynamicAdapter<OrderedRefSetLeaf<std::string>>, BlockCacheDynamicAdapter<std::string>> set(cache, cache, cache, block_manager.get_root());

	set.insert("abc");

	return 0;
}
