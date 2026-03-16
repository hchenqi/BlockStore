#include "BlockStore/core/manager.h"
#include "BlockStore/data/block.h"
#include "CppSerialize/stl/vector.h"

#include <cassert>
#include <unordered_map>
#include <ranges>
#include <random>
#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>


using namespace BlockStore;


struct Item {
	std::vector<block<Item>> list;
};

constexpr auto layout(layout_type<Item>) { return declare(&Item::list); }

constexpr size_t max_list_size = (block_size_limit - sizeof(size_t) * 1) / sizeof(ref_t);


struct ItemView {
	block<Item> ref;
	Item item;

	ItemView(const block<Item>& ref) : ref(ref), item(ref.read([]() { return Item{}; })) {}
};


class Context {
protected:
	block<Item> root;
	std::unordered_map<ref_t, ItemView> tab;
	ref_t focus;
	std::vector<block<Item>> clipboard;

protected:
	Context(block_ref root) : root(std::move(root)), tab(), focus(this->root), clipboard() {
		tab.emplace(this->root, this->root);
	}
};


class Test : private Context {
public:
	Test(block_ref root) : Context(std::move(root)), gen(std::random_device()()) {}

private:
	const Item& get_focus_item() const { return tab.at(focus).item; }
	void update_focus_item(auto f) { ItemView& item_view = tab.at(focus); f(item_view.item); item_view.ref.write(item_view.item); }

public:
	void print() {
		std::cout << "focus: " << focus << " - ";
		for (auto& index : get_focus_item().list) {
			std::cout << index << " ";
		}
		std::cout << std::endl;

		std::cout << "tab: ";
		for (auto& index : std::ranges::views::keys(tab)) {
			std::cout << index << " ";
		}
		std::cout << std::endl;

		std::cout << "clipboard: ";
		for (auto& index : clipboard) {
			std::cout << index << " ";
		}
		std::cout << std::endl;
		std::cout << std::endl;
	}

private:
	mutable std::mt19937 gen;

private:
	size_t random_index(size_t size) const {
		assert(size > 0);
		return std::uniform_int_distribution<>(0, size - 1)(gen);
	}
	auto random_tab() const {
		return std::next(tab.begin(), random_index(tab.size()));
	}
	auto random_child_index() const {
		return std::next(get_focus_item().list.begin(), random_index(get_focus_item().list.size()));
	}
	auto random_clipboard_index() const {
		return std::next(clipboard.begin(), random_index(clipboard.size()));
	}

private:
	void open_random_tab() {
		focus = random_tab()->first;
	}
	void close_random_tab() {
		if (auto it = random_tab(); it->first != root) {
			if (it->first == focus) {
				tab.erase(it);
				open_random_tab();
			} else {
				tab.erase(it);
			}
		}
	}
	void create_random_child() {
		if (get_focus_item().list.size() < max_list_size) {
			update_focus_item([&](Item& item) {
				item.list.emplace_back(root.get_manager().allocate());
			});
		}
	}
	void open_random_child() {
		if (!get_focus_item().list.empty()) {
			auto it = random_child_index();
			tab.emplace(*it, *it);
		}
	}
	void delete_random_child() {
		if (!get_focus_item().list.empty()) {
			update_focus_item([&](Item& item) {
				item.list.erase(random_child_index());
			});
		}
	}
	void copy_random_child() {
		if (!get_focus_item().list.empty()) {
			clipboard.push_back(*random_child_index());
		}
	}
	void paste_random() {
		if (!clipboard.empty() && get_focus_item().list.size() < max_list_size) {
			auto it = random_clipboard_index();
			update_focus_item([&](Item& item) { item.list.push_back(*it); });
			clipboard.erase(it);
		}
	}

private:
	static constexpr std::array operation = {
		&Test::open_random_tab,
		&Test::close_random_tab,
		&Test::create_random_child,
		&Test::open_random_child,
		&Test::delete_random_child,
		&Test::copy_random_child,
		&Test::paste_random
	};
public:
	void random_operation() {
		(this->*operation[random_index(operation.size())])();
	}
};


void print_gc_info(const GCInfo& info) {
	auto phase_to_string = [](GCPhase p) -> const char* {
		switch (p) {
		case GCPhase::Idle: return "Idle";
		case GCPhase::Scanning: return "Scanning";
		case GCPhase::Sweeping: return "Sweeping";
		default: return "Unknown";
		}
	};

	std::cout << "GC Info:" << std::endl;
	std::cout << "  mark: " << info.mark << std::endl;
	std::cout << "  phase: " << phase_to_string(info.phase) << std::endl;
	std::cout << "  block_count_prev: " << info.block_count_prev << std::endl;
	std::cout << "  block_count: " << info.block_count << std::endl;

	if (info.block_count > 0) {
		double marked_pct = (100.0 * static_cast<double>(info.block_count_marked)) / static_cast<double>(info.block_count);
		std::cout << "  block_count_marked: " << info.block_count_marked << " (" << marked_pct << "%)" << std::endl;
	} else {
		std::cout << "  block_count_marked: " << info.block_count_marked << std::endl;
	}

	if (info.phase == GCPhase::Sweeping) {
		std::cout << "  max_id: " << info.max_id << std::endl;

		if (info.max_id > 0 && info.sweeping_id > 0) {
			double sweep_pct = (100.0 * static_cast<double>(info.sweeping_id - 1)) / static_cast<double>(info.max_id);
			std::cout << "  sweeping_id: " << info.sweeping_id << " (" << sweep_pct << "%)" << std::endl;
		} else {
			std::cout << "  sweeping_id: " << info.sweeping_id << std::endl;
		}
	}

	std::cout << std::endl;
}


int main() {
	BlockManager block_manager("graph_test.db");

	std::string command;
	std::atomic<bool> interrupt(true);

	std::thread input([&]() {
		for (;;) {
			while (interrupt) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			std::getline(std::cin, command);
			interrupt = true;
			if (command == "exit") {
				break;
			}
		}
	});

	for (;;) {
		Test test(block_manager.get_root());
		for (;;) {
			interrupt = false;
			while (!interrupt) {
				test.random_operation();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			test.print();

			print_gc_info(block_manager.get_gc_info());

			if (command == "gc") {
				block_manager.gc(GCOption{ [](const GCInfo& info) { print_gc_info(info); return false; } });
			}

			if (command == "gc step") {
				block_manager.gc(GCOption{ [](const GCInfo& info) { print_gc_info(info); return true; }, 1, 1, 1024, 1024 });
			}

			if (command == "gc step deep") {
				block_manager.gc(GCOption{ [](const GCInfo& info) { print_gc_info(info); return true; }, 8, 1024, 16, 1024 });
			}

			if (command == "restart" || command == "exit") {
				break;
			}
		}

		if (command == "exit") {
			break;
		}
	}

	input.join();
	return 0;
}
