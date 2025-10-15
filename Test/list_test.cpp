#include "common.h"
#include "BlockStore/List.h"
#include "CppSerialize/stl/string.h"


using namespace BlockStore;


int main() {
	try {
		block_manager.open_file("list_test.db");
	} catch (const std::exception& e) {
		std::cout << e.what();
		std::remove("list_test.db");
		return 0;
	}

	try {
		List<std::string> list(block_manager.get_root());
		print(list);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		List<std::string> list(block_manager.get_root());
		print(list);

		block_manager.transaction([&] {
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

		block_manager.collect_garbage();

		print(list);

		block_manager.transaction([&] {
			for (int i = 0; i < 5; ++i) {
				list.emplace_front(std::to_string(-i));
			}
		});
		print(list);
	}

	block_cache_shared::clear();
	block_manager.collect_garbage();
	block_manager.collect_garbage();

	{
		List<std::string> list(block_manager.get_root());
		print(list);

		list.clear();
		print(list);
	}

	block_manager.collect_garbage();

	return 0;
}
