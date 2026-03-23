#include "BlockStore/Item/Tree.h"
#include "CppSerialize/stl/string.h"
#include "common.h"

#include <deque>


using namespace BlockStore;


inline void print(const auto& container, auto get = [](const auto& i) { return i.get(); }) {
	for (auto i : container) {
		std::cout << get(i) << ' ';
	}
	std::cout << std::endl;

	// if constexpr (reversible<decltype(container)>) {
	// 	for (auto i : reverse(container)) {
	// 		std::cout << get(i) << ' ';
	// 	}
	// 	std::cout << std::endl;
	// 	std::cout << std::endl;
	// }
}


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
	print(set, [](const auto& i) { return i.read(); });
}


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
	void insert(const block_ref& ref) {
		Base::insert(ref, ref);
	}
};

template<class T, template<class T> class Cache>
void print(UnorderedRefSet<Cache> set) {
	print(set, [](const auto& i) { return static_cast<const block<T>&>(i).read(); });
	//print(set, [](const auto& i) { return i; });
}


int main() {
	BlockManager block_manager("tree_test.db");
	BlockCacheDynamic cache(block_manager);

	{
		block<std::tuple<>>(block_manager.get_root()).write({});
		OrderedRefSet<std::string, BlockCacheDynamicAdapter> set(cache, cache, cache, block_manager.get_root());
		print(set);

		std::deque<std::string> values = { "6", "4", "7", "3", "1", "5", "2", "9", "8" };
		while (!values.empty()) {
			set.insert(values.front()); values.pop_front();
			print(set);
		}

		while (!set.empty()) {
			set.erase(set.begin());
			print(set);
		}

		set.clear();
		print(set);
	}
	cache.sweep();

	{
		block<std::tuple<>>(block_manager.get_root()).write({});
		UnorderedRefSet<BlockCacheDynamicAdapter> set(cache, cache, block_manager.get_root());
		print<std::string>(set);

		std::deque<std::string> values = { "6", "4", "7", "3", "1", "5", "2", "9", "8" };
		while (!values.empty()) {
			set.insert(cache.create<std::string>(values.front()).drop()); values.pop_front();
			print<std::string>(set);
		}

		while (!set.empty()) {
			set.erase(set.begin());
			print<std::string>(set);
		}

		set.clear();
		print<std::string>(set);
	}

	return 0;
}
