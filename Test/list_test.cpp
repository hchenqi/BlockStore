#include "BlockStore/Item/List.h"
#include "CppSerialize/stl/string.h"
#include "common.h"


using namespace BlockStore;


int main() {
	BlockManager block_manager("list_test.db");
	BlockCache cache(block_manager);

	try {
		List<std::string> list(cache, block_manager.get_root());
		print(list);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		List<std::string> list(cache, block_manager.get_root());
		print(list);

		cache.transaction([&] {
			for (int i = 0; i < 10; ++i) {
				list.emplace_back(std::to_string(i));
			}
		});
		print(list);

		list.emplace(++++list.begin(), "1.5");
		print(list);

		list.emplace(list.begin(), "-1");
		print(list);

		list.emplace(list.end(), "10");
		print(list);

		list.pop_front();
		print(list);

		list.pop_back();
		print(list);

		auto it = list.begin();
		it = list.erase(it);
		print(list);

		it = list.erase(--list.end());
		print(list);

		it = list.erase(++list.begin());
		print(list);

		it = list.erase(it);
		print(list);

		it = list.erase(------list.end());
		print(list);

		it = list.erase(it);
		print(list);

		(*--it).set("5.0");
		print(list);

		block_manager.gc(GCOption{});

		print(list);

		cache.transaction([&] {
			for (int i = 0; i < 5; ++i) {
				list.emplace_front(std::to_string(-i));
			}
		});
		print(list);
	}

	cache.sweep();
	block_manager.gc(GCOption{});
	block_manager.gc(GCOption{});

	{
		List<std::string> list(cache, block_manager.get_root());
		print(list);

		list.clear();
		print(list);
	}

	block_manager.gc(GCOption{});

	return 0;
}
