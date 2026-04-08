#include "Dynamic.h"


namespace BlockStore {

namespace Dynamic {

namespace {

std::vector<std::function<std::unique_ptr<ItemView>(DeserializeContext&)>> interpreter_registry = {};

} // namespace


interpreter_ref ItemView::RegisterInterpreter(std::function<std::unique_ptr<ItemView>(DeserializeContext&)> f) {
	interpreter_ref type = interpreter_registry.size();
	interpreter_registry.emplace_back(std::move(f));
	return type;
}

std::unique_ptr<ItemView> ItemView::ConstructInterpreter(interpreter_ref type, DeserializeContext& context) {
	return interpreter_registry[type](context);
}


interpreter_ref EmptyView::type = RegisterInterpreter<EmptyView>();
interpreter_ref BooleanView::type = RegisterInterpreter<BooleanView>();
interpreter_ref IntegerView::type = RegisterInterpreter<IntegerView>();
interpreter_ref StringView::type = RegisterInterpreter<StringView>();
interpreter_ref RefView::type = RegisterInterpreter<RefView>();

interpreter_ref AnyView::type = RegisterInterpreter<AnyView>();
interpreter_ref ArrayView::type = RegisterInterpreter<ArrayView>();
interpreter_ref TupleView::type = RegisterInterpreter<TupleView>();
interpreter_ref UnionView::type = RegisterInterpreter<UnionView>();


namespace {

std::unique_ptr<DescriptorRegistry> descriptor_registry;

} // namespace

interpreter_ref DescriptorAnyView::type = RegisterInterpreter<DescriptorAnyView>();

void DescriptorAnyView::ResetDescriptorRegistry(std::unique_ptr<DescriptorRegistry> registry) { descriptor_registry = std::move(registry); }

DescriptorRegistry& DescriptorAnyView::GetDescriptorRegistry() { return *descriptor_registry; }


} // namespace Dynamic

} // namespace BlockStore
