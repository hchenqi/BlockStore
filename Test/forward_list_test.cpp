#include "common.h"
#include "BlockStore/ForwardList.h"
#include "CppSerialize/stl/string.h"


using namespace BlockStore;


int main() {
	try {
		block_manager.open_file("forward_list_test.db");
	} catch (const std::exception& e) {
		std::cout << e.what();
		std::remove("forward_list_test.db");
		return 0;
	}

	try {
		ForwardList<std::string> forward_list(block_manager.get_root());
		print(forward_list);
	} catch (...) {
		block<std::tuple<>>(block_manager.get_root()).write({});
		return 0;
	}

	{
		ForwardList<std::string> forward_list(block_manager.get_root());
		print(forward_list);

		block_manager.transaction([&] {
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

		block_manager.gc();

		print(forward_list);

		block_manager.transaction([&] {
			for (int i = 0; i < 5; ++i) {
				forward_list.emplace_front(std::to_string(-i));
			}
		});
		print(forward_list);
	}

	block_cache_shared::clear();
	block_manager.gc();
	block_manager.gc();

	{
		ForwardList<std::string> forward_list(block_manager.get_root());
		print(forward_list);

		forward_list.clear();
		print(forward_list);
	}

	block_manager.gc();

	return 0;
}
