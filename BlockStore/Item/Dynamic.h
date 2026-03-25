#include "OrderedRefSet.h"
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

using TypeRegistry = OrderedRefSet<TypeMeta, BlockCacheDynamicAdapter>;


struct Root {
	block_ref type_registry; // TypeRegistry
	block_ref root; // DynamicView

	friend constexpr auto layout(layout_type<Root>) { return declare(&Root::type_registry, &Root::root); }
};


class DynamicView {
protected:
	using TypeView = block_view<TypeMeta, TypeRegistry::KeyCache>;

protected:
	DynamicView(TypeRegistry& type_registry, TypeView type) : type_registry(type_registry), type(std::move(type)) {}

private:
	DynamicView* parent;
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
		std::unique_ptr<DynamicView> child = std::visit([&](const auto& type) { return Construct(type_registry, std::move(meta), type); }, meta.get());
		RegisterChild(*child);
		return child;
	}
private:
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Empty&) {
		return std::make_unique<EmptyView>(type_registry, std::move(type));
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Boolean&) {
		return std::make_unique<BooleanView>(type_registry, std::move(type));
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Integer&) {
		return std::make_unique<IntegerView>(type_registry, std::move(type));
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const String&) {
		return std::make_unique<StringView>(type_registry, std::move(type));
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Ref&) {
		return std::make_unique<RefView>(type_registry, std::move(type));
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Array&) {
		return std::make_unique<ArrayView>(type_registry, std::move(type));
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Tuple& tuple) {
		return std::make_unique<TupleView>(type_registry, std::move(type), tuple);
	}
	static std::unique_ptr<DynamicView> Construct(TypeRegistry& type_registry, TypeView type, const Union&) {
		return std::make_unique<UnionView>(type_registry, std::move(type));
	}
};

class DynamicViewRoot {
public:
	DynamicViewRoot(TypeRegistry& type_registry, block_ref root) {

	}

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
	TupleView(TypeRegistry& type_registry, TypeView type, const Tuple& tuple) : DynamicView(type_registry, std::move(type)) {
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


} // namespace BlockStore
