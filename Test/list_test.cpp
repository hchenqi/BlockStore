#include "BlockStore/block_manager.h"

#include <iostream>


using namespace BlockStore;


struct Node {
	int number = 0;
	std::optional<block<Node>> next = std::nullopt;
};

auto layout(layout_type<Node>) { return declare(&Node::number, &Node::next); }


void PrintList(block<std::optional<block<Node>>> root) {
	for (auto current = root.read(); current != std::nullopt;) {
		auto data = current.value().read();
		std::cout << data.number << std::endl;
		current = data.next;
	}
}

void AppendList(block<std::optional<block<Node>>> root) {
	auto data = root.read();
	block<Node> next;
	next.write(data == std::nullopt ? Node{} : Node{ data.value().read().number + 1, data.value() });
	root.write(next);
}

int main() {
	block_manager.open_file("block_test.db");
	block<std::optional<block<Node>>> root = block_manager.get_root();
	PrintList(root);
	AppendList(root);
}