#include "BlockStore/Deque.h"

#include <iostream>


using namespace BlockStore;


void print(auto container) {
	for (auto i : container) {
		std::cout << i << ' ';
	}
	std::cout << std::endl;
}

int main() {
	try {
		block_manager.open_file("deque_test.db");
	} catch (const std::exception& e) {
		std::cout << e.what();
		std::remove("deque_test.db");
		return 0;
	}

	try {
		Deque<uint64> deque(block_manager.get_root());
		print(deque);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		Deque<uint64> deque(block_manager.get_root());
		block_manager.transaction([&]() {
			for (int i = 0; i < 1024; ++i) {
				deque.emplace_back(i);
			}
		});
		print(deque);

		deque.pop_front();
		print(deque);

		deque.pop_front();
		print(deque);

		deque.pop_front();
		print(deque);

		deque.pop_back();
		print(deque);

		block_manager.transaction([&]() {
			for (int i = 0; i < 10; ++i) {
				deque.emplace_front(-i);
			}
		});
		print(deque);

		deque.clear();
		print(deque);
	}

	block_cache_shared::clear();
	block_manager.collect_garbage();

	return 0;
}
