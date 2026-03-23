#include "BlockStore/Item/OrderedRefSet.h"
#include "BlockStore/Item/UnorderedRefSet.h"
#include "CppSerialize/stl/string.h"

#include <deque>
#include <iostream>


using namespace BlockStore;


void print(const auto& container, auto get) {
	for (auto i : container) {
		std::cout << get(i) << ' ';
	}
	std::cout << std::endl;
}


template<class Key, template<class T> class Cache>
void print(OrderedRefSet<Key, Cache> set) {
	print(set, [](const auto& i) { return i.read(); });
}

template<class T, template<class T> class Cache>
void print(UnorderedRefSet<Cache> set) {
	print(set, [](const auto& i) { return static_cast<const block<T>&>(i).read(); });
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
