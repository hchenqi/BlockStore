#include "BlockStore/block_manager.h"

#include <iostream>


using namespace BlockStore;


struct Node {
	int number;
	block_ref next;
};

auto layout(layout_type<Node>) { return declare(&Node::number, &Node::next); }


void PrintList(const block_ref& root) {
	for (block_ref node = root; !node.empty();) {
		Node block; node.read(block);
		std::cout << block.number << std::endl;
		node = block.next;
	}
}

void AppendList(block_ref& root) {
	if (root.empty()) {
		root.write(Node{ 0 });
	} else {
		block_ref next = root;
		root.clear();
		root.write(Node{ next.read<Node>().number + 1, next });
	}
}

int main() {
	block_manager.open("block_test.db");
	block_ref root = block_manager.get_root();
	PrintList(root);
	AppendList(root);
	block_manager.set_root(root);
	block_manager.close();
}