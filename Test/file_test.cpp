#include "BlockStore/core/manager.h"
#include "BlockStore/data/block.h"
#include "CppSerialize/stl/string.h"

#include <iostream>


using namespace BlockStore;


int main() {
	BlockManager block_manager("file_test.db");
	block<std::string> root = block_manager.get_root();
	std::cout << root.read() << std::endl;
	root.write("Hello world!");
	std::cout << root.read() << std::endl;
	block_manager.gc(GCOption{});
	return 0;
}
