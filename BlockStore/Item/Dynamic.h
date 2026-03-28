#include "OrderedRefSet.h"
#include "../utility/type_map.h"
#include "CppSerialize/stl/variant.h"
#include "CppSerialize/stl/string.h"


namespace BlockStore {

namespace Dynamic {


struct TypeMeta;

struct TypeBase { auto operator<=>(const TypeBase&) const = default; };

struct Empty : TypeBase {};
struct Boolean : TypeBase {};
struct Integer : TypeBase {};
struct String : TypeBase {};
struct Ref : TypeBase {};
struct Array : std::pair<size_t, block<TypeMeta>> { using layout_base = std::pair<size_t, block<TypeMeta>>; };
struct Tuple : std::vector<block<TypeMeta>> { using layout_base = std::vector<block<TypeMeta>>; };
struct Union : std::vector<block<TypeMeta>> { using layout_base = std::vector<block<TypeMeta>>; };

struct TypeMeta : std::variant<Empty, Boolean, Integer, String, Ref, Array, Tuple, Union> { using layout_base = std::variant<Empty, Boolean, Integer, String, Ref, Array, Tuple, Union>; };


using TypeRegistry = OrderedRefSet<TypeMeta, BlockCacheDynamicAdapter>;


class ItemData : private block_ref, private block_ref_deserialize {
private:
	friend class Item;
private:
	ItemData(TypeRegistry& type_registry, block_ref&& other) : block_ref(std::move(other)), type_registry(type_registry) {}
private:
	TypeRegistry& type_registry;
	std::vector<std::byte> data;
	std::vector<std::byte>::const_iterator index;
	std::vector<ref_t> ref;
private:
	void read_begin() {
		data = block_ref::read();
		if (data.empty()) {
			write_begin();
			write(type_registry.insert(TypeMeta(Empty())));
			write_end();
		}
		index = data.begin();
	}
	void read_end() {
		assert(index == data.end());
	}
protected:
	void read(layout_trivial auto& object) {
		if (data.end() < index + sizeof(object)) {
			throw std::runtime_error("deserialize error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(index, index + sizeof(object), bytes.begin());
		object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
		index += sizeof(object);
	}
	void read(block_ref& object) {
		ref_t ref;
		read(ref);
		object = block_ref_deserialize::construct(get_manager(), ref);
	}
	void read(auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::write([&](auto& item) { read(item); }, object);
	}
private:
	void write_begin() {
		data.clear();
		ref.clear();
	}
	void write_end() {
		block_ref::write(data, ref);
	}
protected:
	void write(const layout_trivial auto& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(object);
		data.insert(data.end(), bytes.begin(), bytes.end());
	}
	void write(const block_ref& object) {
		if (&get_manager() != &object.get_manager()) {
			throw std::invalid_argument("block manager mismatch");
		}
		write(static_cast<ref_t>(object));
		ref.push_back(object);
	}
	void write(const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { write(item); }, object);
	}
};

struct DeserializeContext : ItemData {
	template<class T> T access() { T object; read(object); return object; }
};

struct SerializeContext : ItemData {
	void access(const auto& object) { write(object); }
};


class ViewControl {
private:
	friend class RootViewControl;
	friend class ViewBase;
private:
	using TypeView = block_view<TypeMeta, TypeRegistry::KeyCache>;
private:
	ViewControl(TypeRegistry& type_registry, block<TypeMeta> ref, DeserializeContext& context) :
		type_registry(type_registry),
		type(type_registry.insert(std::move(ref))),
		view(ConstructView(type.get(), context)) {}
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
	ViewBase* parent = nullptr;
	std::unique_ptr<ViewBase> view;
private:
	std::unique_ptr<ViewBase> ConstructView(const TypeMeta& type, DeserializeContext& context);
private:
	RootViewControl& AsRoot();
private:
	void TypeUpdated();
	void DataUpdated();
private:
	void Serialize(SerializeContext& context) const;
};

class RootViewControl : public ViewControl {
private:
	friend class Item;
	friend class ViewControl;
private:
	RootViewControl(Item& item, TypeRegistry& type_registry, DeserializeContext& context) : ViewControl(type_registry, context.access<block<TypeMeta>>(), context), item(item) {}
private:
	Item& item;
private:
	using ViewControl::type;
private:
	void OnTypeUpdate();
	void OnDataUpdate();
private:
	using ViewControl::Serialize;
};

inline RootViewControl& ViewControl::AsRoot() { return static_cast<RootViewControl&>(*this); }


class Item {
private:
	friend class RootViewControl;
	friend class ViewBase;
public:
	Item(TypeRegistry& type_registry, block_ref ref) : data(type_registry, std::move(ref)), root(*this, type_registry, DeserializeBegin()) { DeserializeEnd(); }
private:
	ItemData data;
	RootViewControl root;
private:
	DeserializeContext& DeserializeBegin() {
		data.read_begin();
		return static_cast<DeserializeContext&>(data);
	}
	void DeserializeEnd() {
		data.read_end();
	}
	void Serialize() {
		data.write_begin();
		root.Serialize(static_cast<SerializeContext&>(data));
		data.write_end();
	}
private:
	void OnTypeUpdate() { Serialize(); }
	void OnDataUpdate() { Serialize(); }
};

inline void RootViewControl::OnTypeUpdate() { item.OnTypeUpdate(); }
inline void RootViewControl::OnDataUpdate() { item.OnDataUpdate(); }


class ViewBase {
private:
	friend class ViewControl;
protected:
	ViewBase(ViewControl& control) : control(control) {}
private:
	ViewControl& control;
protected:
	void RegisterChild(ViewControl& child) { if (child.parent) { throw std::invalid_argument("child already has a parent"); } child.parent = this; }
	void UnregisterChild(ViewControl& child) { VerifyChild(child); child.parent = nullptr; }
	void VerifyChild(const ViewControl& child) const { if (child.parent != this) { throw std::invalid_argument("not a child"); } }
protected:
	ViewControl ConstructChild(block<TypeMeta> ref, DeserializeContext& context) { ViewControl child(control.type_registry, std::move(ref), context); RegisterChild(child); return child; }
protected:
	template<class T> const T& GetType() const { return control.GetType<T>(); }
	template<class T> void SetType(T type) { return control.SetType<T>(std::move(type)); }
protected:
	block<TypeMeta> GetChildType(ViewControl& child) const { VerifyChild(child); return child.type; }
protected:
	void TypeUpdated() { control.TypeUpdated(); }
	void DataUpdated() { control.DataUpdated(); }
protected:
	virtual void OnChildTypeUpdate(ViewControl& child) {}
	virtual void OnChildDataUpdate(ViewControl& child) { DataUpdated(); }
protected:
	void SerializeChild(SerializeContext& context, const ViewControl& child) const { VerifyChild(child); child.Serialize(context); }
protected:
	virtual void Serialize(SerializeContext& context) const {}
};

inline void ViewControl::TypeUpdated() { parent ? parent->OnChildTypeUpdate(*this) : AsRoot().OnTypeUpdate(); }
inline void ViewControl::DataUpdated() { parent ? parent->OnChildDataUpdate(*this) : AsRoot().OnDataUpdate(); }
inline void ViewControl::Serialize(SerializeContext& context) const { view->Serialize(context); }


class EmptyView : public ViewBase {
public:
	EmptyView(ViewControl& control, DeserializeContext& context) : ViewBase(control) {}
};

class BooleanView : public ViewBase {
public:
	BooleanView(ViewControl& control, DeserializeContext& context) : ViewBase(control), value(context.access<bool>()) {}
protected:
	bool value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class IntegerView : public ViewBase {
public:
	IntegerView(ViewControl& control, DeserializeContext& context) : ViewBase(control), value(context.access<uint64>()) {}
protected:
	uint64 value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class StringView : public ViewBase {
public:
	StringView(ViewControl& control, DeserializeContext& context) : ViewBase(control), value(context.access<std::string>()) {}
protected:
	std::string value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class RefView : public ViewBase {
public:
	RefView(ViewControl& control, DeserializeContext& context) : ViewBase(control), value(context.access<block_ref>()) {}
protected:
	block_ref value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class ArrayView : public ViewBase {
public:
	ArrayView(ViewControl& control, DeserializeContext& context) : ViewBase(control) {
		const Array& array = GetType<Array>();
		child_list.reserve(array.first);
		for (size_t i = 0; i < array.first; ++i) {
			child_list.emplace_back(ConstructChild(array.second, context));
		}
	}
protected:
	std::vector<ViewControl> child_list;
protected:
	virtual void OnChildTypeUpdate(ViewControl& child) override {
		Array type;
		type.first = child_list.size();
		type.second = GetChildType(child);
		SetType(std::move(type));
	}
protected:
	virtual void Serialize(SerializeContext& context) const override {
		for (auto& child : child_list) {
			SerializeChild(context, child);
		}
	}
};

class TupleView : public ViewBase {
public:
	TupleView(ViewControl& control, DeserializeContext& context) : ViewBase(control) {
		const Tuple& tuple = GetType<Tuple>();
		child_list.reserve(tuple.size());
		for (const auto& child : tuple) {
			child_list.emplace_back(ConstructChild(child, context));
		}
	}
protected:
	std::vector<ViewControl> child_list;
protected:
	virtual void OnChildTypeUpdate(ViewControl& child) override {
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

class UnionView : public ViewBase {
public:
	UnionView(ViewControl& control, DeserializeContext& context) : ViewBase(control) {}
protected:
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

template<class T>
using ViewType = typename View<T>::Type;

inline std::unique_ptr<ViewBase> ViewControl::ConstructView(const TypeMeta& type, DeserializeContext& context) {
	return std::visit([&](const auto& type) -> std::unique_ptr<ViewBase> { return std::make_unique<ViewType<std::remove_cvref_t<decltype(type)>>>(*this, context); }, type);
}


} // namespace Dynamic

} // namespace BlockStore
