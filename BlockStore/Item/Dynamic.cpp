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


const interpreter_ref EmptyView::type = RegisterInterpreter<EmptyView>();
const interpreter_ref BooleanView::type = RegisterInterpreter<BooleanView>();
const interpreter_ref IntegerView::type = RegisterInterpreter<IntegerView>();
const interpreter_ref StringView::type = RegisterInterpreter<StringView>();
const interpreter_ref RefView::type = RegisterInterpreter<RefView>();

const interpreter_ref AnyView::type = RegisterInterpreter<AnyView>();
const interpreter_ref ArrayView::type = RegisterInterpreter<ArrayView>();
const interpreter_ref TupleView::type = RegisterInterpreter<TupleView>();
const interpreter_ref UnionView::type = RegisterInterpreter<UnionView>();


namespace Descriptor {

namespace {

std::unique_ptr<DescriptorRegistry> descriptor_registry;

} // namespace


const interpreter_ref DescriptorAnyView::type = RegisterInterpreter<DescriptorAnyView>();

void DescriptorView::ResetDescriptorRegistry(std::unique_ptr<DescriptorRegistry> registry) { descriptor_registry = std::move(registry); }

DescriptorRegistry& DescriptorView::GetDescriptorRegistry() { return *descriptor_registry; }


} // namespace Descriptor

} // namespace Dynamic

} // namespace BlockStore
