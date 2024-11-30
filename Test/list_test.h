#include "BlockStore/block_manager.h"

#include <iostream>


using namespace BlockStore;


struct Node {
	int number;
	block_ref next;
};

auto layout(layout_type<Node>) { return declare(&Node::number, &Node::next); }


void PrintList(const block_ref& root) {
	for (block<Node> node(root); !node.empty();) {
		Node data = node.read();
		std::cout << data.number << std::endl;
		node = block<Node>(data.next);
	}
}

void AppendList(block_ref& root) {
	block<Node> node(root);
	if (node.empty()) {
		node.write(Node{ 0 });
		root = node;
	} else {
		block<Node> next;
		next.write(Node{ node.read().number + 1, node });
		root = next;
	}
}

int main() {
	block_manager.open_file("block_test.db");
	block_ref root = block_manager.get_root();
	PrintList(root);
	AppendList(root);
	block_manager.set_root(root);
	block_manager.close_file();
}