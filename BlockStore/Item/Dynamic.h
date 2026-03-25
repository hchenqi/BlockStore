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


class DynamicViewControl {
private:
	friend class DynamicView;
private:
	using TypeView = block_view<TypeMeta, TypeRegistry::KeyCache>;

protected:
	DynamicViewControl(TypeRegistry& type_registry, block<TypeMeta> ref) :
		type_registry(type_registry),
		type(type_registry.insert(std::move(ref))),
		view(ConstructView(type.get())) {}

private:
	TypeRegistry& type_registry;
	TypeView type;
private:
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

private:
	DynamicView* parent = nullptr;
	std::unique_ptr<DynamicView> view;
private:
	std::unique_ptr<DynamicView> ConstructView(const TypeMeta& type) {
		return std::visit([&](const auto& type) { return std::make_unique<ViewType<std::remove_cvref_t<decltype(type)>>>(*this); }, type);
	}
private:
	DynamicRootViewControl& AsRoot() { return static_cast<DynamicRootViewControl&>(*this); }
private:
	void TypeUpdated() { parent ? parent->OnChildTypeUpdate(*this) : AsRoot().OnTypeUpdate(); }
	void DataUpdated() { parent ? parent->OnChildDataUpdate(*this) : AsRoot().OnDataUpdate(); }
};

class DynamicRootViewControl : public DynamicViewControl {
public:
	DynamicRootViewControl(TypeRegistry& type_registry, block<TypeMeta> ref) : DynamicViewControl(type_registry, std::move(ref)) {}

private:

private:
	friend class DynamicViewControl;
private:
	void OnTypeUpdate() {}
	void OnDataUpdate() {}
};

class DynamicItem {
public:
	DynamicItem(TypeRegistry& type_registry, block_ref ref) :
		data(BlockCacheLocal<Data>::read(std::move(ref), [&] { return std::make_pair(type_registry.insert(TypeMeta(Empty())), std::vector<std::byte>()); })),
		root(type_registry, data.get().first) {}

private:
	using Data = std::pair<block<TypeMeta>, std::vector<std::byte>>;
private:
	block_view_local<Data> data;

private:
	friend class DynamicView;
private:
	struct DeserializeContext : block_ref_deserialize {
	public:
		DeserializeContext(std::vector<std::byte> data) : data(std::move(data)), index(0) {}
	private:
		std::vector<std::byte> data;
		size_t index;
	public:
		void access(auto& value) {
			std::array<std::byte, sizeof(object)> bytes;
			std::copy(context.index, context.index + sizeof(object), bytes.begin());
			object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
			context.index += sizeof(object);
		}
	};

private:
	DynamicRootViewControl root;
};


class DynamicView {
private:
	friend class DynamicViewControl;

protected:
	DynamicView(DynamicViewControl& control) : control(control) {}

private:
	DynamicViewControl& control;

protected:
	void RegisterChild(DynamicViewControl& child) { if (child.parent) { throw std::invalid_argument("child already has a parent"); } child.parent = this; }
	void UnregisterChild(DynamicViewControl& child) { VerifyChild(child); child.parent = nullptr; }
	void VerifyChild(DynamicViewControl& child) const { if (child.parent != this) { throw std::invalid_argument("not a child"); } }
protected:
	DynamicViewControl ConstructChild(block<TypeMeta> ref) { return DynamicViewControl(control.type_registry, std::move(ref)); }

protected:
	template<class T> const T& GetType() const { return control.GetType<T>(); }
	template<class T> void SetType(T type) { return control.SetType<T>(); }
protected:
	block<TypeMeta> GetChildType(DynamicViewControl& child) const { VerifyChild(child); return child.type; }

protected:
	void TypeUpdated() { control.TypeUpdated(); }
	void DataUpdated() { control.DataUpdated(); }
protected:
	virtual void OnChildTypeUpdate(DynamicViewControl& child) {}
	virtual void OnChildDataUpdate(DynamicViewControl& child) { DataUpdated(); }

protected:
	using SerializeContext = DynamicItem::SerializeContext;
	using DeserializeContext = DynamicItem::DeserializeContext;
protected:
	void SerializeChild(SerializeContext& context, DynamicView& child) const { VerifyChild(child); child.Serialize(context); }
	void DeserializeChild(DeserializeContext& context, DynamicView& child) const { VerifyChild(child); child.Deserialize(context); }
protected:
	virtual void Serialize(SerializeContext& context) const {}
	virtual void Deserialize(DeserializeContext& context) {}
};

class EmptyView : public DynamicView {
public:
	EmptyView(DynamicViewControl& control) : DynamicView(control) {}

protected:
	virtual void Serialize(SerializeContext& context) const override {}
	virtual void Deserialize(DeserializeContext& context) override {}
};

class BooleanView : public DynamicView {
public:
	BooleanView(DynamicViewControl& control) : DynamicView(control) {}
protected:
	bool value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
	virtual void Deserialize(DeserializeContext& context) override { context.access(value); }
};

class IntegerView : public DynamicView {
public:
	IntegerView(DynamicViewControl& control) : DynamicView(control) {}
protected:
	uint64 value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
	virtual void Deserialize(DeserializeContext& context) override { context.access(value); }
};

class StringView : public DynamicView {
public:
	StringView(DynamicViewControl& control) : DynamicView(control) {}
protected:
	std::string value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
	virtual void Deserialize(DeserializeContext& context) override { context.access(value); }
};

class RefView : public DynamicView {
public:
	RefView(DynamicViewControl& control) : DynamicView(control) {}
protected:
	block_ref value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
	virtual void Deserialize(DeserializeContext& context) override { context.access(value); }
};

class ArrayView : public DynamicView {
public:
	ArrayView(DynamicViewControl& control) : DynamicView(control) {
		const Array& array = GetType<Array>();
		child_list.reserve(array.first);
		for (size_t i = 0; i < array.first; ++i) {
			child_list.emplace_back(ConstructChild(array.second));
		}
	}

protected:
	std::vector<DynamicViewControl> child_list;

protected:
	virtual void OnChildTypeUpdate(DynamicView& child) {
		Array type;
		type.first = child_list.size();
		type.second = GetChildType(child);
		SetType(std::move(type));
	}

	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
	virtual void Deserialize(DeserializeContext& context) override { context.access(value); }
};

class TupleView : public DynamicView {
public:
	TupleView(DynamicViewControl& control) : DynamicView(control) {
		const Tuple& tuple = GetType<Tuple>();
		child_list.reserve(tuple.size());
		for (const auto& child : tuple) {
			child_list.emplace_back(ConstructChild(child));
		}
	}

protected:
	std::vector<DynamicViewControl> child_list;

protected:
	virtual void OnChildTypeUpdate(DynamicViewControl& child) override {
		Tuple type; type.reserve(child_list.size());
		for (auto& child : child_list) {
			type.emplace_back(GetChildType(child));
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
