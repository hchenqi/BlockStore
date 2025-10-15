#include "BlockStore/block_cache.h"
#include "BlockStore/block_manager.h"
#include "CppSerialize/stl/string.h"
#include "CppSerialize/stl/vector.h"

#include <cassert>
#include <unordered_map>
#include <ranges>
#include <random>
#include <algorithm>
#include <array>
#include <iostream>
#include <atomic>
#include <thread>


using namespace BlockStore;


struct Item {
	std::string text;
	std::vector<block<Item>> list;
};

constexpr auto layout(layout_type<Item>) { return declare(&Item::text, &Item::list); }


class Context {
protected:
	block<Item> root;
	std::unordered_map<index_t, block_cache<Item>> tab;
	block_cache<Item> focus;
	std::vector<block<Item>> clipboard;

protected:
	Context(const block_ref& root) : root(root), tab(), focus(root), clipboard() {
		tab.emplace(root, root);
	}
};


class Test : private Context {
public:
	Test(const block_ref& root) : Context(root), gen(std::random_device()()) {}

public:
	void print() {
		std::cout << "focus: " << focus << " - ";
		for (auto& index : focus.get().list) {
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
		return std::next(focus.get().list.begin(), random_index(focus.get().list.size()));
	}
	auto random_clipboard_index() const {
		return std::next(clipboard.begin(), random_index(clipboard.size()));
	}

private:
	void open_random_tab() {
		focus = random_tab()->second;
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
		focus.update([&](Item& item) {
			item.list.emplace_back();
		});
	}
	void open_random_child() {
		if (!focus.get().list.empty()) {
			auto it = random_child_index();
			tab.emplace(*it, *it);
		}
	}
	void delete_random_children() {
		if (!focus.get().list.empty()) {
			focus.update([&](Item& item) {
				std::shuffle(item.list.begin(), item.list.end(), gen);
				item.list.erase(random_child_index(), item.list.end());
			});
		}
	}
	void copy_random_children() {
		if (!focus.get().list.empty()) {
			focus.update([&](Item& item) { std::shuffle(item.list.begin(), item.list.end(), gen); });
			clipboard.insert(clipboard.end(), random_child_index(), focus.get().list.end());
		}
	}
	void paste_random() {
		if (!clipboard.empty()) {
			auto it = random_clipboard_index();
			focus.update([&](Item& item) { item.list.insert(item.list.end(), it, clipboard.cend()); });
			clipboard.erase(it, clipboard.end());
		}
	}

private:
	static constexpr std::array operation = {
		&Test::open_random_tab,
		&Test::close_random_tab,
		&Test::create_random_child,
		&Test::open_random_child,
		&Test::delete_random_children,
		&Test::copy_random_children,
		&Test::paste_random
	};
public:
	void random_operation() {
		(this->*operation[random_index(operation.size())])();
	}
};


int main() {
	block_manager.open_file("graph_test.db");

	std::string command;
	std::atomic<bool> interrupt(false);

	std::thread input([&]() {
		for (;;) {
			std::getline(std::cin, command);
			interrupt = true;
			if (command == "exit") {
				break;
			}
		}
	});

	for (;;) {
		{
			Test test(block_manager.get_root());
			for (;;) {
				while (!interrupt) {
					test.random_operation();
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
				interrupt = false;
				test.print();
				if (command == "gc" || command == "exit") {
					break;
				}
			}
		}
		block_cache_shared::clear();
		block_manager.collect_garbage();
		if (command == "exit") {
			break;
		}
	}

	input.join();
	return 0;
}
