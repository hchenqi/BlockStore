#include "BlockStore/block_manager.h"
#include "BlockStore/block.h"


using namespace BlockStore;


int main() {
	block_manager.open_file("block_test.db");
	block<uint64> root = block_manager.get_root();
	root.write(0);
	block_manager.collect_garbage();
	return 0;
}
