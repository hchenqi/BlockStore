#include "BlockStore/Item/Dynamic.h"


using namespace BlockStore;


int main() {
	BlockManager block_manager("dynamic_test.db");
	BlockCacheDynamic cache(block_manager);

	block_view_local<Root> root = BlockCacheLocal<Root>(block_manager).read(block_manager.get_root(), [&]() { return Root{ block_manager.allocate(), block_manager.allocate() }; });

	TypeRegistry type_registry(cache, cache, cache, root.get().type_registry);


	return 0;
}
