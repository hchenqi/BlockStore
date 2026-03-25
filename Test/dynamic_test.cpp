#include "BlockStore/Item/Dynamic.h"


using namespace BlockStore;


int main() {
	BlockManager block_manager("dynamic_test.db");
	BlockCacheDynamic cache(block_manager);

	DynamicViewRoot root(block_manager, cache, block_manager.get_root());


	return 0;
}
