#include "BlockStore/Item/Dynamic.h"


using namespace BlockStore;


struct Root {
	block_ref descriptor_registry;
	block_ref root;

	friend constexpr auto layout(layout_type<Root>) { return declare(&Root::descriptor_registry, &Root::root); }
};


int main() {
	{
		using namespace Dynamic;

		BlockManager block_manager("dynamic_test.db");

		BlockView block_view(AnyView::type, block_manager.get_root(), [&]() {
			return std::make_unique<AnyView>(
				std::make_unique<UnionView>(
					std::vector<interpreter_ref>{ StringView::type, ArrayView::type, TupleView::type, UnionView::type },
					std::make_unique<TupleView>(
						std::make_unique<EmptyView>(),
						std::make_unique<ArrayView>(
							IntegerView::type,
							std::make_unique<IntegerView>(1),
							std::make_unique<IntegerView>(2),
							std::make_unique<IntegerView>(3)
						),
						std::make_unique<StringView>("hello"),
						std::make_unique<RefView>(
							std::make_unique<BlockView>(AnyView::type, block_manager.allocate(), []() {
								return std::make_unique<AnyView>(
									std::make_unique<StringView>("world")
								);
							})
						)
					)
				)
			);
		});
	}

	{
		using namespace Dynamic;
		using namespace Dynamic::Descriptor;

		BlockManager block_manager("dynamic_descriptor_test.db");
		BlockCacheDynamic cache(block_manager);

		block_view_local<Root> root = BlockCacheLocal<Root>::read(block_manager.get_root(), [&]() { return Root{ block_manager.allocate(), block_manager.allocate() }; });

		DescriptorView::ResetDescriptorRegistry(std::make_unique<DescriptorRegistry>(cache, cache, cache, root.get().descriptor_registry));

		BlockView block_view(DescriptorAnyView::type, root.get().root, [&]() {
			return std::make_unique<DescriptorAnyView>(
				std::make_unique<UnionDescriptorView>(
					UnionDescriptor{
						DescriptorView::RegisterDescriptor(TupleDescriptor{
							DescriptorView::RegisterDescriptor(BasicDescriptor{ EmptyView::type }),
							DescriptorView::RegisterDescriptor(ArrayDescriptor{
								3,
								DescriptorView::RegisterDescriptor(BasicDescriptor{ IntegerView::type })
							}),
							DescriptorView::RegisterDescriptor(BasicDescriptor{ StringView::type }),
						}),
					},
					std::make_unique<TupleDescriptorView>(
						std::make_unique<BasicDescriptorView>(std::make_unique<EmptyView>()),
						std::make_unique<ArrayDescriptorView>(
							DescriptorView::RegisterDescriptor(BasicDescriptor{ IntegerView::type }),
							std::make_unique<BasicDescriptorView>(std::make_unique<IntegerView>(1)),
							std::make_unique<BasicDescriptorView>(std::make_unique<IntegerView>(2)),
							std::make_unique<BasicDescriptorView>(std::make_unique<IntegerView>(3))
						),
						std::make_unique<BasicDescriptorView>(std::make_unique<StringView>("hello"))
					)
				)
			);
		});

		DescriptorView::ResetDescriptorRegistry();
	}

	return 0;
}
