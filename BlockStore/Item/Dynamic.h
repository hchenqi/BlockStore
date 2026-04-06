#include "OrderedRefSet.h"
#include "../utility/type_map.h"
#include "CppSerialize/stl/variant.h"
#include "CppSerialize/stl/string.h"


namespace BlockStore {

namespace Dynamic {


struct TypeMeta;

using type_ref = block<TypeMeta>;

struct Any : std::monostate {};
struct Empty : std::monostate {};
struct Boolean : std::monostate {};
struct Integer : std::monostate {};
struct String : std::monostate {};
struct Ref : std::tuple<type_ref> { using layout_base = std::tuple<type_ref>; };
struct Array : std::tuple<size_t, type_ref> { using layout_base = std::tuple<size_t, type_ref>; };
struct Tuple : std::vector<type_ref> { using layout_base = std::vector<type_ref>; };
struct Union : std::vector<type_ref> { using layout_base = std::vector<type_ref>; };

struct TypeMeta : std::variant<Any, Empty, Boolean, Integer, String, Ref, Array, Tuple, Union> { using layout_base = std::variant<Any, Empty, Boolean, Integer, String, Ref, Array, Tuple, Union>; };


using TypeRegistry = OrderedRefSet<TypeMeta, BlockCacheDynamicAdapter>;

using type_view = block_view<TypeMeta, TypeRegistry::KeyCache>;


class ItemViewControl {
private:
	friend class BlockView;
	friend class ItemView;
private:
	ItemViewControl(TypeRegistry& type_registry, type_view type, DeserializeContext& context) : type_registry(type_registry), type(std::move(type)), view(ConstructItemView(context)) {}
private:
	TypeRegistry& type_registry;
	type_view type;
private:
	template<class T>
	const T& GetType() const {
		return std::get<T>(type.get());
	}
	template<class T>
	void SetType(T type) {
		type_view new_type = type_registry.insert(TypeMeta(std::move(type)));
		if (this->type != new_type) {
			this->type = new_type;
			TypeUpdated();
		}
	}
private:
	ItemView* parent = nullptr;
	std::unique_ptr<ItemView> view;
private:
	std::unique_ptr<ItemView> ConstructItemView(DeserializeContext& context);
private:
	BlockView& AsBlockView();
private:
	void TypeUpdated();
	void DataUpdated();
private:
	void Serialize(SerializeContext& context) const;
};

class BlockView : private ItemViewControl {
private:
	friend class ItemViewControl;
private:
	BlockView(TypeRegistry& type_registry, type_view type, DeserializeContext context, block_ref&& ref) : ItemViewControl(type_registry, std::move(type), context), ref(std::move(ref)) {}
public:
	BlockView(TypeRegistry& type_registry, type_view type, block_ref ref) : BlockView(type_registry, std::move(type), GetDeserializeContext(ref, type_registry), std::move(ref)) {}
private:
	block_ref ref;
private:
	static DeserializeContext GetDeserializeContext(block_ref& ref, TypeRegistry& type_registry) {
		auto data = ref.read();
		if (data.empty()) {
			auto [init_data, init_ref_list] = SerializeContext(ref.get_manager()).access(type_registry.insert(TypeMeta(Empty()))).Get();
			ref.write(init_data, init_ref_list);
			data = std::move(init_data);
		}
		return DeserializeContext(ref.get_manager(), std::move(data));
	}
private:
	void Serialize() {
		SerializeContext serialize_context(ref.get_manager());
		ItemViewControl::Serialize(serialize_context);
		auto [data, ref_list] = serialize_context.Get();
		if (data.size() > block_size_limit) {
			throw std::invalid_argument("block size exceeds limit");
		}
		ref.write(data, ref_list);
	}
private:
	void OnTypeUpdate() { Serialize(); }
	void OnDataUpdate() { Serialize(); }
};

inline BlockView& ItemViewControl::AsBlockView() { return static_cast<BlockView&>(*this); }


class ItemView {
private:
	friend class ItemViewControl;
protected:
	ItemView(ItemViewControl& control) : control(control) {}
private:
	ItemViewControl& control;
protected:
	void RegisterChild(ItemViewControl& child) { if (child.parent) { throw std::invalid_argument("child already has a parent"); } child.parent = this; }
	void UnregisterChild(ItemViewControl& child) { VerifyChild(child); child.parent = nullptr; }
	void VerifyChild(const ItemViewControl& child) const { if (child.parent != this) { throw std::invalid_argument("not a child"); } }
protected:
	ItemViewControl ConstructChild(type_ref type_ref, DeserializeContext& context) { ItemViewControl child(control.type_registry, control.type_registry.insert(std::move(type_ref)), context); RegisterChild(child); return child; }
protected:
	template<class T> const T& GetType() const { return control.GetType<T>(); }
	template<class T> void SetType(T type) { return control.SetType<T>(std::move(type)); }
protected:
	type_ref GetChildType(const ItemViewControl& child) const { VerifyChild(child); return child.type; }
protected:
	void TypeUpdated() { control.TypeUpdated(); }
	void DataUpdated() { control.DataUpdated(); }
protected:
	virtual void OnChildTypeUpdate(ItemViewControl& child) {}
	virtual void OnChildDataUpdate(ItemViewControl& child) { DataUpdated(); }
protected:
	void SerializeChild(SerializeContext& context, const ItemViewControl& child) const { VerifyChild(child); child.Serialize(context); }
protected:
	virtual void Serialize(SerializeContext& context) const {}
};

inline void ItemViewControl::TypeUpdated() { parent ? parent->OnChildTypeUpdate(*this) : AsBlockView().OnTypeUpdate(); }
inline void ItemViewControl::DataUpdated() { parent ? parent->OnChildDataUpdate(*this) : AsBlockView().OnDataUpdate(); }
inline void ItemViewControl::Serialize(SerializeContext& context) const { view->Serialize(context); }


class AnyView : public ItemView {
public:
	AnyView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<type_ref>()), child(ConstructChild(value, context)) {}
protected:
	type_ref value;
protected:
	ItemViewControl child;
protected:
	virtual void OnChildTypeUpdate(ItemViewControl& child) override { value = GetChildType(child); DataUpdated(); }
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); SerializeChild(context, child); }
};

class EmptyView : public ItemView {
public:
	EmptyView(ItemViewControl& control, DeserializeContext& context) : ItemView(control) {}
};

class BooleanView : public ItemView {
public:
	BooleanView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<bool>()) {}
protected:
	bool value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class IntegerView : public ItemView {
public:
	IntegerView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<uint64>()) {}
protected:
	uint64 value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class StringView : public ItemView {
public:
	StringView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<std::string>()) {}
protected:
	std::string value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class RefView : public ItemView {
public:
	RefView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<block_ref>()) {
		const auto& [type] = static_cast<const std::tuple<type_ref>&>(GetType<Ref>());
	}
protected:
	block_ref value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class ArrayView : public ItemView {
public:
	ArrayView(ItemViewControl& control, DeserializeContext& context) : ItemView(control) {
		const auto& [size, type] = static_cast<const std::tuple<size_t, type_ref>&>(GetType<Array>());
		child_list.reserve(size);
		for (size_t i = 0; i < size; ++i) {
			child_list.emplace_back(ConstructChild(type, context));
		}
	}
protected:
	std::vector<ItemViewControl> child_list;
protected:
	virtual void OnChildTypeUpdate(ItemViewControl& child) override {
		SetType(Array(std::make_tuple(child_list.size(), GetChildType(child))));
	}
protected:
	virtual void Serialize(SerializeContext& context) const override {
		for (auto& child : child_list) {
			SerializeChild(context, child);
		}
	}
};

class TupleView : public ItemView {
public:
	TupleView(ItemViewControl& control, DeserializeContext& context) : ItemView(control) {
		const Tuple& type_list = GetType<Tuple>();
		child_list.reserve(type_list.size());
		for (const auto& child : type_list) {
			child_list.emplace_back(ConstructChild(child, context));
		}
	}
protected:
	std::vector<ItemViewControl> child_list;
protected:
	virtual void OnChildTypeUpdate(ItemViewControl& child) override {
		Tuple type; type.reserve(child_list.size());
		for (auto& child : child_list) {
			type.emplace_back(GetChildType(child));
		}
		SetType(std::move(type));
	}
protected:
	virtual void Serialize(SerializeContext& context) const override {
		for (auto& child : child_list) {
			SerializeChild(context, child);
		}
	}
};

class UnionView : public ItemView {
public:
	UnionView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<size_t>()), child(ConstructChild(GetType<Union>()[value], context)) {}
protected:
	size_t value;
protected:
	ItemViewControl child;
protected:
	virtual void OnChildTypeUpdate(ItemViewControl& child) override {
		Union type = GetType<Union>();
		type[value] = GetChildType(child);
		SetType(std::move(type));
	}
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); SerializeChild(context, child); }
};


using TypeViewMap = TypeMap<
	TypeMapEntry<Any, AnyView>,
	TypeMapEntry<Empty, EmptyView>,
	TypeMapEntry<Boolean, BooleanView>,
	TypeMapEntry<Integer, IntegerView>,
	TypeMapEntry<String, StringView>,
	TypeMapEntry<Ref, RefView>,
	TypeMapEntry<Array, ArrayView>,
	TypeMapEntry<Tuple, TupleView>,
	TypeMapEntry<Union, UnionView>
>;

template<class T>
struct View {
	using Type = MappedType<TypeViewMap, T>;
};

template<class T>
using ViewType = typename View<T>::Type;

inline std::unique_ptr<ItemView> ItemViewControl::ConstructItemView(DeserializeContext& context) {
	return std::visit([&](const auto& type) -> std::unique_ptr<ItemView> { return std::make_unique<ViewType<std::remove_cvref_t<decltype(type)>>>(*this, context); }, type.get());
}


} // namespace Dynamic

} // namespace BlockStore
