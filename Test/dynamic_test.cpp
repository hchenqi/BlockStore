#include "BlockStore/Item/Dynamic.h"


using namespace BlockStore;


struct Root {
	block_ref type_registry;
	block_ref root;

	friend constexpr auto layout(layout_type<Root>) { return declare(&Root::type_registry, &Root::root); }
};


int main() {
	BlockManager block_manager("dynamic_test.db");
	BlockCacheDynamic cache(block_manager);

	block_view_local<Root> root = BlockCacheLocal<Root>::read(block_manager.get_root(), [&]() { return Root{ block_manager.allocate(), block_manager.allocate() }; });

	{
		using namespace Dynamic;

		TypeRegistry type_registry(cache, cache, cache, root.get().type_registry);

		BlockView block_view(type_registry, type_registry.insert(TypeMeta(Any())), root.get().root);
	}

	return 0;
}
