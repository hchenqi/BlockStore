#include "BlockStore/block_manager.h"


using namespace BlockStore;


int main() {
	block_manager.open_file("block_test.db");
	block_manager.set_root({});
	block_manager.collect_garbage();
	return 0;
}
