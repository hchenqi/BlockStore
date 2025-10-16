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

constexpr size_t max_list_size = (block_size_limit - sizeof(size_t) * 2) / sizeof(index_t);


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
		if (focus.get().list.size() < max_list_size) {
			focus.update([&](Item& item) {
				item.list.emplace_back();
			});
		}
	}
	void open_random_child() {
		if (!focus.get().list.empty()) {
			auto it = random_child_index();
			tab.emplace(*it, *it);
		}
	}
	void delete_random_child() {
		if (!focus.get().list.empty()) {
			focus.update([&](Item& item) {
				item.list.erase(random_child_index());
			});
		}
	}
	void copy_random_child() {
		if (!focus.get().list.empty()) {
			clipboard.push_back(*random_child_index());
		}
	}
	void paste_random() {
		if (!clipboard.empty() && focus.get().list.size() < max_list_size) {
			auto it = random_clipboard_index();
			focus.update([&](Item& item) { item.list.push_back(*it); });
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


void print(const BlockManager::GCInfo& info) {
	auto phase_to_string = [](BlockManager::GCPhase p) -> const char* {
		switch (p) {
		case BlockManager::GCPhase::Idle: return "Idle";
		case BlockManager::GCPhase::Scanning: return "Scanning";
		case BlockManager::GCPhase::Sweeping: return "Sweeping";
		default: return "Unknown";
		}
	};

	std::cout << "GC Info:" << std::endl;
	std::cout << "  mark: " << (info.mark ? "true" : "false") << std::endl;
	std::cout << "  phase: " << phase_to_string(info.phase) << std::endl;
	std::cout << "  block_count_prev: " << info.block_count_prev << std::endl;
	std::cout << "  block_count: " << info.block_count << std::endl;

	if (info.block_count > 0) {
		double marked_pct = (100.0 * static_cast<double>(info.block_count_marked)) / static_cast<double>(info.block_count);
		std::cout << "  block_count_marked: " << info.block_count_marked << " (" << marked_pct << "%)" << std::endl;
	} else {
		std::cout << "  block_count_marked: " << info.block_count_marked << std::endl;
	}

	if (info.phase == BlockManager::GCPhase::Sweeping) {
		std::cout << "  max_index: " << info.max_index << std::endl;

		if (info.max_index > 0 && info.sweeping_index > 0) {
			double sweep_pct = (100.0 * static_cast<double>(info.sweeping_index - 1)) / static_cast<double>(info.max_index);
			std::cout << "  sweeping_index: " << info.sweeping_index << " (" << sweep_pct << "%)" << std::endl;
		} else {
			std::cout << "  sweeping_index: " << info.sweeping_index << std::endl;
		}
	}

	std::cout << std::endl;
}


int main() {
	block_manager.open_file("graph_test.db");

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
		{
			Test test(block_manager.get_root());
			for (;;) {
				interrupt = false;
				while (!interrupt) {
					test.random_operation();
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				test.print();

				if (command == "gc" || command == "exit") {
					break;
				}
			}
		}

		block_cache_shared::clear();

		std::cout << "gc begin" << std::endl;

		class Callback : public BlockManager::GCCallback {
			virtual void Notify(const GCInfo& info) override {
				print(info);
			}
			virtual bool Interrupt(const GCInfo& info) override {
				print(info);
				return false;
			}
		}callback;

		block_manager.gc(callback);

		std::cout << "gc end" << std::endl;

		if (command == "exit") {
			break;
		}
	}

	input.join();
	return 0;
}
