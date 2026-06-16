#include "BlockStore/Item/OrderedRefSet.h"
#include "BlockStore/utility/type_map.h"
#include "CppSerialize/stl/string.h"

#include <iostream>


using namespace BlockStore;


template<template<class T> class Cache>
using StringTable = OrderedRefSet<std::string, Cache>;


using CacheMap = TypeMap<
	TypeMapEntry<std::string, BlockCache<std::string>>
>;

template<class T>
using Cache = MappedTypeOr<CacheMap, T, BlockCacheDynamicAdapter<T>>;


void print(const auto& string_table) {
	for (auto i : string_table) {
		std::cout << i << ": " << i.read() << std::endl;
	}
	std::cout<< std::endl;
}


int main() {
	BlockManager block_manager("string_test.db");

	BlockCacheDynamic node_cache(block_manager);
	BlockCacheDynamic leaf_cache(block_manager);
	BlockCache<std::string> key_cache(block_manager);

	StringTable<Cache> string_table(node_cache, leaf_cache, key_cache, block_manager.get_root());
	print(string_table);

	for (;;) {
		std::cout << "> ";

		std::string str;
		if (!std::getline(std::cin, str)) {
			break;
		}

		auto block = string_table.insert(str);

		std::cout << block << ": " << block.get() << std::endl;
	}

	return 0;
}
