#include "OrderedRefSet.h"
#include "../utility/type_map.h"
#include "CppSerialize/stl/variant.h"
#include "CppSerialize/stl/string.h"


namespace BlockStore {

namespace Dynamic {


struct TypeMeta;

struct Empty : std::monostate {};
struct Boolean : std::monostate {};
struct Integer : std::monostate {};
struct String : std::monostate {};
struct Ref : std::monostate {};
struct Array : std::pair<size_t, block<TypeMeta>> { using layout_base = std::pair<size_t, block<TypeMeta>>; };
struct Tuple : std::vector<block<TypeMeta>> { using layout_base = std::vector<block<TypeMeta>>; };
struct Union : std::vector<block<TypeMeta>> { using layout_base = std::vector<block<TypeMeta>>; };

struct TypeMeta : std::variant<Empty, Boolean, Integer, String, Ref, Array, Tuple, Union> { using layout_base = std::variant<Empty, Boolean, Integer, String, Ref, Array, Tuple, Union>; };


using TypeRegistry = OrderedRefSet<TypeMeta, BlockCacheDynamicAdapter>;


class BlockData : private block_ref, private block_ref_deserialize {
private:
	friend class BlockView;
private:
	BlockData(TypeRegistry& type_registry, block_ref other) : block_ref(std::move(other)), type_registry(type_registry) {}
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

struct DeserializeContext : BlockData {
	template<class T> T access() { T object; read(object); return object; }
};

struct SerializeContext : BlockData {
	void access(const auto& object) { write(object); }
};


class ItemViewControl {
private:
	friend class BlockViewControl;
	friend class ItemView;
private:
	using TypeView = block_view<TypeMeta, TypeRegistry::KeyCache>;
private:
	ItemViewControl(TypeRegistry& type_registry, block<TypeMeta> ref, DeserializeContext& context) :
		type_registry(type_registry),
		type(type_registry.insert(std::move(ref))),
		view(ConstructItemView(type.get(), context)) {}
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
	ItemView* parent = nullptr;
	std::unique_ptr<ItemView> view;
private:
	std::unique_ptr<ItemView> ConstructItemView(const TypeMeta& type, DeserializeContext& context);
private:
	BlockViewControl& AsBlockViewControl();
private:
	void TypeUpdated();
	void DataUpdated();
private:
	void Serialize(SerializeContext& context) const;
};

class BlockViewControl : public ItemViewControl {
private:
	friend class BlockView;
	friend class ItemViewControl;
private:
	BlockViewControl(BlockView& block_view, TypeRegistry& type_registry, DeserializeContext& context) : ItemViewControl(type_registry, context.access<block<TypeMeta>>(), context), block_view(block_view) {}
private:
	BlockView& block_view;
private:
	using ItemViewControl::type;
private:
	void OnTypeUpdate();
	void OnDataUpdate();
private:
	using ItemViewControl::Serialize;
};

inline BlockViewControl& ItemViewControl::AsBlockViewControl() { return static_cast<BlockViewControl&>(*this); }


class BlockView {
private:
	friend class BlockViewControl;
	friend class ItemView;
public:
	BlockView(TypeRegistry& type_registry, block_ref ref) : data(type_registry, std::move(ref)), control(*this, type_registry, DeserializeBegin()) { DeserializeEnd(); }
private:
	BlockData data;
	BlockViewControl control;
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
		control.Serialize(static_cast<SerializeContext&>(data));
		data.write_end();
	}
private:
	void OnTypeUpdate() { Serialize(); }
	void OnDataUpdate() { Serialize(); }
};

inline void BlockViewControl::OnTypeUpdate() { block_view.OnTypeUpdate(); }
inline void BlockViewControl::OnDataUpdate() { block_view.OnDataUpdate(); }


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
	ItemViewControl ConstructChild(block<TypeMeta> ref, DeserializeContext& context) { ItemViewControl child(control.type_registry, std::move(ref), context); RegisterChild(child); return child; }
protected:
	template<class T> const T& GetType() const { return control.GetType<T>(); }
	template<class T> void SetType(T type) { return control.SetType<T>(std::move(type)); }
protected:
	block<TypeMeta> GetChildType(ItemViewControl& child) const { VerifyChild(child); return child.type; }
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

inline void ItemViewControl::TypeUpdated() { parent ? parent->OnChildTypeUpdate(*this) : AsBlockViewControl().OnTypeUpdate(); }
inline void ItemViewControl::DataUpdated() { parent ? parent->OnChildDataUpdate(*this) : AsBlockViewControl().OnDataUpdate(); }
inline void ItemViewControl::Serialize(SerializeContext& context) const { view->Serialize(context); }


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
	RefView(ItemViewControl& control, DeserializeContext& context) : ItemView(control), value(context.access<block_ref>()) {}
protected:
	block_ref value;
protected:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }
};

class ArrayView : public ItemView {
public:
	ArrayView(ItemViewControl& control, DeserializeContext& context) : ItemView(control) {
		const Array& array = GetType<Array>();
		child_list.reserve(array.first);
		for (size_t i = 0; i < array.first; ++i) {
			child_list.emplace_back(ConstructChild(array.second, context));
		}
	}
protected:
	std::vector<ItemViewControl> child_list;
protected:
	virtual void OnChildTypeUpdate(ItemViewControl& child) override {
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

class TupleView : public ItemView {
public:
	TupleView(ItemViewControl& control, DeserializeContext& context) : ItemView(control) {
		const Tuple& tuple = GetType<Tuple>();
		child_list.reserve(tuple.size());
		for (const auto& child : tuple) {
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
	UnionView(ItemViewControl& control, DeserializeContext& context) : ItemView(control) {}
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

inline std::unique_ptr<ItemView> ItemViewControl::ConstructItemView(const TypeMeta& type, DeserializeContext& context) {
	return std::visit([&](const auto& type) -> std::unique_ptr<ItemView> { return std::make_unique<ViewType<std::remove_cvref_t<decltype(type)>>>(*this, context); }, type);
}


} // namespace Dynamic

} // namespace BlockStore
