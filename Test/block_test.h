#pragma once

#include "BlockStore/block_manager.h"
#include "BlockStore/layout_traits_stl.h"


using namespace BlockStore;


struct Node {
	int number = 0;
	std::variant<BlockRef<Node>, nullptr_t> next = nullptr;
};

auto layout(layout_type<Node>) { return declare(&Node::number, &Node::next); }

using RootRef = BlockRef<Node>;



int main() {
	BlockManager block_manager("block_test.db");
	BlockRef<
}