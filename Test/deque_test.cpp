#include "BlockStore/Item/Deque.h"
#include "common.h"


using namespace BlockStore;


int main() {
	BlockManager block_manager("deque_test.db");
	BlockCacheLocal<DequeNode<int>> cache(block_manager);

	try {
		Deque<int, BlockCacheLocal> deque(cache, block_manager.get_root());
		print(deque);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		Deque<int, BlockCacheLocal> deque(cache, block_manager.get_root());
		cache.transaction([&]() {
			for (int i = 0; i < 10; ++i) {
				deque.emplace_back(i);
			}
		});
		print(deque);

		for (int i = 8; i > 0; --i) {
			deque.pop_front();
		}
		print(deque);

		deque.pop_back();
		print(deque);

		cache.transaction([&]() {
			for (int i = 0; i < 10; ++i) {
				deque.emplace_front(i);
			}
		});
		print(deque);

		auto it = deque.begin() += 10;
		for (int i = 7; i > 0; --i) {
			it = deque.emplace(it, i);
			print(deque);
		}

		it -= 5;
		for (int i = 5; i > 0; --i) {
			it = deque.erase(it);
			print(deque);
		}

		(*it).update([](auto& v) { v = 0; });
		print(deque);

		deque.clear();
		print(deque);
	}

	cache.sweep();
	block_manager.gc(GCOption{});

	return 0;
}
