#include "BlockStore/List.h"
#include "CppSerialize/stl/string.h"

#include <iostream>


using namespace BlockStore;


void print_list(auto list) {
	for (auto i : list) {
		std::cout << i << ' ';
	}
	std::cout << std::endl;
}

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
		print_list(list);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		List<std::string> list(block_manager.get_root());
		block_manager.transaction([&]() {
			for (int i = 0; i < 10; ++i) {
				list.push_back(std::to_string(i));
			}
		});
		print_list(list);

		list.pop_front();
		print_list(list);

		list.pop_back();
		print_list(list);

		block_manager.collect_garbage();

		print_list(list);

		block_manager.transaction([&]() {
			for (int i = 0; i < 5; ++i) {
				list.push_front(std::to_string(i));
			}
		});
		print_list(list);
	}

	block_manager.collect_garbage();

	{
		List<std::string> list(block_manager.get_root());
		list.clear();
		print_list(list);
	}

	block_manager.collect_garbage();

	return 0;
}
