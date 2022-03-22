#include "BlockStore/block_manager.h"

#include <iostream>


using namespace BlockStore;


struct Node {
	int number = 0;
	BlockRef<Node> next;
};

auto layout(layout_type<Node>) { return declare(&Node::number, &Node::next); }

using RootRef = BlockRef<Node>;


void PrintList(const RootRef& root) {
	for (BlockRef<Node> node = root; !node.empty();) {
		auto& block = node.read();
		std::cout << block.number << std::endl;
		node = block.next;
	}
}


void AppendList(RootRef& root) {
	BlockRef<Node> next = root;
	root = block_ref();
	auto& block = root.write();
	block.number = next.read().number + 1;
	block.next = next;
}

int main() {
	BlockManager block_manager("block_test.db");
	BlockRef<Node> root_ref = block_manager.GetRootRef();
	PrintList(root_ref);
	AppendList(root_ref);
	block_manager.SetRootRef();
}