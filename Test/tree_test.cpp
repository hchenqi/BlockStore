#include "BlockStore/Item/Tree.h"
#include "CppSerialize/stl/string.h"
#include "common.h"


using namespace BlockStore;


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
	void insert(const Key& key) {
		Base::insert(key, key_cache.create(key).drop());
	}
};


template<class Key, template<class T> class Cache>
void print(OrderedRefSet<Key, Cache> set) {
	for (auto i : set) {
		std::cout << i.read() << ' ';
	}
	std::cout << std::endl;
}


int main() {
	BlockManager block_manager("tree_test.db");
	BlockCacheDynamic cache(block_manager);

	OrderedRefSet<std::string, BlockCacheDynamicAdapter> set(cache, cache, cache, block_manager.get_root());
	print(set);

	set.clear();
	print(set);

	set.insert("6");
	print(set);

	set.insert("4");
	set.insert("7");
	set.insert("3");
	print(set);

	return 0;
}
