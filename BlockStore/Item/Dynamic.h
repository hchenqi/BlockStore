#include "OrderedRefSet.h"
#include "../utility/type_map.h"
#include "CppSerialize/stl/variant.h"


namespace BlockStore {


struct TypeMeta;

struct Empty {};
struct Boolean {};
struct Integer {};
struct String {};
struct Ref {};
struct Array : std::pair<size_t, block<TypeMeta>> {};
struct Tuple : std::vector<block<TypeMeta>> {};
struct Union : std::vector<block<TypeMeta>> {};

struct TypeMeta : std::variant<Empty, Boolean, Integer, String, Ref, Array, Tuple, Union> {};


template<class T>
struct View;

template<class T>
using ViewType = typename View<T>::Type;


using TypeRegistry = OrderedRefSet<TypeMeta, BlockCacheDynamicAdapter>;


class DynamicView {
protected:
	using TypeView = block_view<TypeMeta, TypeRegistry::KeyCache>;

protected:
	DynamicView(TypeRegistry& type_registry, TypeView type) : type_registry(type_registry), type(std::move(type)) {}

private:
	DynamicView* parent = nullptr;
protected:
	bool HasParent() const { return parent != nullptr; }
	DynamicView& GetParent() const { if (!HasParent()) { throw std::invalid_argument("parent doesn't exist"); } return *parent; }
protected:
	void RegisterChild(DynamicView& child) { if (child.HasParent()) { throw std::invalid_argument("child already has a parent"); } child.parent = this; }
	void UnregisterChild(DynamicView& child) { VerifyChild(child); child.parent = nullptr; }
	void VerifyChild(DynamicView& child) const { if (child.parent != this) { throw std::invalid_argument("not a child"); } }

private:
	TypeRegistry& type_registry;
	TypeView type;
protected:
	block<TypeMeta> GetChildType(DynamicView& child) const { VerifyChild(child); return child.type; }
private:
	void TypeUpdated() { if (HasParent()) { GetParent().OnChildTypeUpdate(*this); } }
protected:
	virtual void OnChildTypeUpdate(DynamicView& child) {}
protected:
	template<class T>
	const T& GetType() const {
		return std::get<T>(type.get());
	}
	template<class T>
	void SetType(T type) {
		TypeView new_type = type_registry.insert(TypeMeta(std::move(type)));
		if (this->type != new_type) {
			this->type = new_type;
			TypeUpdated();
		}
	}

protected:
	void DataUpdated() { if (HasParent()) { GetParent().OnChildDataUpdate(*this); } }
protected:
	virtual void OnChildDataUpdate(DynamicView& child) { DataUpdated(); }
protected:
	void SerializeChild(SerializeContext& context, DynamicView& child) const { VerifyChild(child); child.Serialize(context); }
	void DeserializeChild(DeserializeContext& context, DynamicView& child) const { VerifyChild(child); child.Deserialize(context); }
protected:
	virtual void Serialize(SerializeContext& context) const {}
	virtual void Deserialize(DeserializeContext& context) {}

protected:
	std::unique_ptr<DynamicView> ConstructChild(block<TypeMeta> ref) {
		TypeView meta = type_registry.insert(std::move(ref));
		std::unique_ptr<DynamicView> child = std::visit([&](const auto& type) {
			return std::make_unique<ViewType<std::decay_t<decltype(type)>>>(type_registry, std::move(meta));
		}, meta.get());
		RegisterChild(*child);
		return child;
	}
};


class DynamicViewControl {

private:
	
	
};

class DynamicViewRoot : public DynamicView {
private:
	struct Data {
		block_ref type_registry;
		block_ref root;

		friend constexpr auto layout(layout_type<Data>) { return declare(&Data::type_registry, &Data::root); }
	};

public:
	DynamicViewRoot(BlockManager& block_manager, BlockCacheDynamic& cache, block_ref root) :
		DynamicView(type_registry, cache.create)
		data(BlockCacheLocal<Data>::read(std::move(root), [&]() { return Data{ block_manager.allocate(), block_manager.allocate() }; })),
		type_registry(cache, cache, cache, data.get().type_registry) {}

	block_view_local<Data> data;
	TypeRegistry type_registry;

private:
	std::unique_ptr<DynamicView> root;

};

class EmptyView : public DynamicView {
protected:
	virtual void Serialize(SerializeContext& context) const override {}
	virtual void Deserialize(DeserializeContext& context) override {}
};

class BooleanView : public DynamicView {
protected:
	bool value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class IntegerView : public DynamicView {
protected:
	uint64 value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class StringView : public DynamicView {
protected:
	std::string value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class RefView : public DynamicView {
protected:
	block_ref value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class ArrayView : public DynamicView {
public:
	ArrayView(TypeRegistry& type_registry, TypeView type, const Array& array) : DynamicView(type_registry, std::move(type)) {
		child_list.reserve(array.first);
		for (size_t i = 0; i < array.first; ++i) {
			child_list.emplace_back(ConstructChild(array.second));
		}
	}

protected:
	std::vector<std::unique_ptr<DynamicView>> child_list;

protected:
	virtual void OnChildTypeUpdate(DynamicView& child) {
		Array type;
		type.first = child_list.size();
		type.second = GetChildType(child);
		SetType(std::move(type));
	}
};

class TupleView : public DynamicView {
public:
	TupleView(TypeRegistry& type_registry, TypeView type) : DynamicView(type_registry, std::move(type)) {
		const Tuple& tuple = GetType<Tuple>();
		child_list.reserve(tuple.size());
		for (const auto& child : tuple) {
			child_list.emplace_back(ConstructChild(child));
		}
	}

protected:
	std::vector<std::unique_ptr<DynamicView>> child_list;

protected:
	virtual void OnChildTypeUpdate(DynamicView& child) {
		Tuple type; type.reserve(child_list.size());
		for (auto& child : child_list) {
			type.emplace_back(GetChildType(*child));
		}
		SetType(std::move(type));
	}

private:
	virtual void Serialize(SerializeContext& context) const override {
		for (auto& child : child_list) {
			SerializeChild(context, *child);
		}
	}
	virtual void Deserialize(DeserializeContext& context) override {
		for (auto& child : child_list) {
			DeserializeChild(context, *child);
		}
	}
};

class UnionView : public DynamicView {

};


using TypeViewMap = TypeMap<
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


} // namespace BlockStore
