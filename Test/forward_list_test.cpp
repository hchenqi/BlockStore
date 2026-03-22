#include "BlockStore/Item/ForwardList.h"
#include "CppSerialize/stl/string.h"
#include "common.h"


using namespace BlockStore;


int main() {
	BlockManager block_manager("forward_list_test.db");

	BlockCache<ForwardListNode<std::string>> cache(block_manager);

	try {
		ForwardList<std::string, BlockCache> forward_list(cache, block_manager.get_root());
		print(forward_list);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		ForwardList<std::string, BlockCache> forward_list(cache, block_manager.get_root());
		print(forward_list);

		cache.transaction([&] {
			for (int i = 9; i >= 0; --i) {
				forward_list.emplace_front(std::to_string(i));
			}
		});
		print(forward_list);

		forward_list.emplace_after(++++forward_list.before_begin(), "1.5");
		print(forward_list);

		forward_list.emplace_after(forward_list.before_begin(), "-1");
		print(forward_list);

		forward_list.pop_front();
		print(forward_list);

		auto it = forward_list.before_begin();
		it = forward_list.erase_after(it);
		print(forward_list);

		it = forward_list.erase_after(++forward_list.before_begin());
		print(forward_list);

		auto& value = (*++it).update([](auto& value) { value += ".5"; });
		print(forward_list);

		it = forward_list.erase_after(it);
		print(forward_list);

		it = forward_list.erase_after(it);
		print(forward_list);

		block_manager.gc(GCOption{});

		print(forward_list);

		cache.transaction([&] {
			for (int i = 0; i < 5; ++i) {
				forward_list.emplace_front(std::to_string(-i));
			}
		});
		print(forward_list);
	}

	cache.sweep();
	block_manager.gc(GCOption{});
	block_manager.gc(GCOption{});

	{
		ForwardList<std::string, BlockCache> forward_list(cache, block_manager.get_root());
		print(forward_list);

		forward_list.clear();
		print(forward_list);
	}

	block_manager.gc(GCOption{});

	return 0;
}
