#include "BlockStore/block_manager.h"
#include "BlockStore/block.h"

#include <iostream>


using namespace BlockStore;


int main() {
	try {
		block_manager.open_file("block_test.db");
	} catch (const std::exception& e) {
		std::cout << e.what();
		std::remove("block_test.db");
		return 0;
	}
	block<uint64> root = block_manager.get_root();
	root.write(0);
	block_manager.collect_garbage();
	return 0;
}
