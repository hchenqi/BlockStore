#include "BlockStore/block_manager.h"

#include <iostream>


using namespace BlockStore;


struct Node {
	int number = 0;
	block_ref<Node> next;
};

auto layout(layout_type<Node>) { return declare(&Node::number, &Node::next); }


void PrintList(const block_ref<Node>& root) {
	for (block_ref<Node> node = root; !node.empty();) {
		auto& block = node.read();
		std::cout << block.number << std::endl;
		node = block.next;
	}
}

void AppendList(block_ref<Node>& root) {
	if (root.empty()) {
		root.write().number = 0;
	} else {
		block_ref<Node> next = root;
		root.clear();
		auto& block = root.write();
		block.number = next.read().number + 1;
		block.next = next;
	}
	root.save();
}

int main() {
	block_manager.open("block_test.db");
	block_ref<Node> root_ref = block_manager.get_root();
	PrintList(root_ref);
	AppendList(root_ref);
	block_manager.set_root(root_ref);
	block_manager.clear_cache();
	block_manager.close();
}