#include "OrderedRefSet.h"
#include "../utility/type_map.h"
#include "CppSerialize/stl/variant.h"
#include "CppSerialize/stl/string.h"

#include <functional>


namespace BlockStore {

namespace Dynamic {

using interpreter_ref = size_t;


class ItemView {
protected:
	ItemView() {}
public:
	virtual ~ItemView() {}

private:
	static interpreter_ref RegisterInterpreter(std::function<std::unique_ptr<ItemView>(DeserializeContext&)> f);
	static std::unique_ptr<ItemView> ConstructInterpreter(interpreter_ref type, DeserializeContext& context);
protected:
	template<class Interpreter>
	static interpreter_ref RegisterInterpreter() {
		return RegisterInterpreter([](DeserializeContext& context) { return std::make_unique<Interpreter>(context); });
	}

private:
	virtual interpreter_ref GetType() const = 0;

private:
	ItemView* parent = nullptr;
protected:
	void RegisterChild(ItemView& child) { if (child.parent) { throw std::invalid_argument("child already has a parent"); } child.parent = this; }
	void UnregisterChild(ItemView& child) { VerifyChild(child); child.parent = nullptr; }
	void VerifyChild(const ItemView& child) const { if (child.parent != this) { throw std::invalid_argument("not a child"); } }
protected:
	std::unique_ptr<ItemView> ConstructChild(interpreter_ref type, DeserializeContext& context) { auto child = ConstructInterpreter(type, context); RegisterChild(*child); return child; }
	interpreter_ref GetChildType(const ItemView& child) const { VerifyChild(child); return child.GetType(); }

protected:
	void DataUpdated() const { if (parent) { parent->OnChildDataUpdate(*this); } }
	void SerializeChild(SerializeContext& context, const ItemView& child) const { VerifyChild(child); child.Serialize(context); }
protected:
	virtual void OnChildDataUpdate(const ItemView& child) { DataUpdated(); }
	virtual void Serialize(SerializeContext& context) const {}
};


class BlockView : private block_ref, private ItemView {
private:
	friend class RefView;

public:
	BlockView(interpreter_ref type, block_ref ref) : block_ref(std::move(ref)) {
		if (auto data = read(); data.empty()) {
			throw std::invalid_argument("block data uninitialized");
		} else {
			DeserializeContext context(get_manager(), std::move(data));
			item = ConstructChild(type, context);
		}
	}
	BlockView(interpreter_ref type, block_ref ref, std::function<std::unique_ptr<ItemView>()> init) : block_ref(std::move(ref)) {
		if (auto data = read(); data.empty()) {
			item = init();
			RegisterChild(*item);
			if (type != GetChildType(*item)) {
				throw std::invalid_argument("child type mismatch");
			}
			Serialize();
		} else {
			DeserializeContext context(get_manager(), std::move(data));
			item = ConstructChild(type, context);
		}
	}

private:
	virtual interpreter_ref GetType() const override { return GetChildType(*item); }

private:
	std::unique_ptr<ItemView> item;
private:
	void Serialize() {
		SerializeContext context(get_manager());
		Serialize(context);
		auto [data, ref_list] = context.Get();
		if (data.size() > block_size_limit) {
			throw std::invalid_argument("block size exceeds limit");
		}
		write(data, ref_list);
	}

private:
	virtual void OnChildDataUpdate(const ItemView& child) override { Serialize(); }
	virtual void Serialize(SerializeContext& context) const override { SerializeChild(context, *item); }
};


class EmptyView : public ItemView {
public:
	EmptyView() {}
	EmptyView(DeserializeContext& context) : ItemView() {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }
};

class BooleanView : public ItemView {
public:
	BooleanView(bool value) : value(value) {}
	BooleanView(DeserializeContext& context) : value(context.access<bool>()) {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	bool value;
private:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }

public:
	void Set(bool value) {
		if (this->value != value) {
			this->value = value;
			DataUpdated();
		}
	}
};

class IntegerView : public ItemView {
public:
	IntegerView(int value) : value(value) {}
	IntegerView(DeserializeContext& context) : value(context.access<int>()) {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	int value;
private:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }

public:
	void Set(int value) {
		if (this->value != value) {
			this->value = value;
			DataUpdated();
		}
	}
};

class StringView : public ItemView {
public:
	StringView(std::string value) : value(std::move(value)) {}
	StringView(DeserializeContext& context) : value(context.access<std::string>()) {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	std::string value;
private:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }

public:
	void Set(std::string value) {
		if (this->value != value) {
			this->value = value;
			DataUpdated();
		}
	}
};

class RefView : public ItemView {
public:
	RefView(std::unique_ptr<BlockView> block_view) { ResetBlockView(std::move(block_view)); }
	RefView(std::pair<interpreter_ref, block_ref> value) : value(std::move(value)) {}
	RefView(DeserializeContext& context) : value(context.access<std::pair<interpreter_ref, block_ref>>()) {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	std::pair<interpreter_ref, block_ref> value;
private:
	virtual void Serialize(SerializeContext& context) const override { context.access(value); }

public:
	void Set(std::pair<interpreter_ref, block_ref> value) {
		if (this->value != value) {
			CollapseBlockView();
			this->value = value;
			DataUpdated();
		}
	}

private:
	std::unique_ptr<BlockView> block_view;
public:
	void ResetBlockView(std::unique_ptr<BlockView> block_view) {
		Set(std::make_pair(block_view->GetType(), block_ref(*block_view)));
		this->block_view = std::move(block_view);
	}
	void ExpandBlockView() {
		if (block_view == nullptr) {
			const auto& [type, ref] = value;
			block_view = std::make_unique<BlockView>(type, ref);
		}
	}
	void CollapseBlockView() {
		block_view.reset();
	}
};


class AnyView : public ItemView {
public:
	AnyView(std::unique_ptr<ItemView> child) { Set(std::move(child)); }
	AnyView(DeserializeContext& context) : child_type(context.access<interpreter_ref>()), child(ConstructChild(child_type, context)) {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	interpreter_ref child_type;
	std::unique_ptr<ItemView> child;
private:
	virtual void Serialize(SerializeContext& context) const override {
		context.access(child_type);
		SerializeChild(context, *child);
	}

public:
	void Set(std::unique_ptr<ItemView> child) {
		RegisterChild(*child);
		child_type = GetChildType(*child);
		this->child = std::move(child);
		DataUpdated();
	}
};

class ArrayView : public ItemView {
public:
	ArrayView(interpreter_ref child_type, std::vector<std::unique_ptr<ItemView>> child_list) { Set(child_type, std::move(child_list)); }
	ArrayView(interpreter_ref child_type, auto... child) : ArrayView(child_type, [&]() {
		std::vector<std::unique_ptr<ItemView>> child_list; child_list.reserve(sizeof...(child));
		(child_list.emplace_back(std::move(child)), ...);
		return child_list;
	}()) {}
	ArrayView(DeserializeContext& context) : descriptor(context.access<std::pair<size_t, interpreter_ref>>()) {
		auto [size, child_type] = descriptor;
		child_list.reserve(size);
		for (size_t i = 0; i < size; ++i) {
			child_list.emplace_back(ConstructChild(child_type, context));
		}
	}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	std::pair<size_t, interpreter_ref> descriptor;
	std::vector<std::unique_ptr<ItemView>> child_list;
private:
	virtual void Serialize(SerializeContext& context) const override {
		context.access(descriptor);
		for (auto& child : child_list) {
			SerializeChild(context, *child);
		}
	}

public:
	void Set(interpreter_ref child_type, std::vector<std::unique_ptr<ItemView>> child_list) {
		for (size_t i = 0; i < child_list.size(); ++i) {
			RegisterChild(*child_list[i]);
			if (child_type != GetChildType(*child_list[i])) {
				throw std::invalid_argument("child_type mismatch");
			}
		}
		descriptor = std::make_pair(child_list.size(), child_type);
		this->child_list = std::move(child_list);
		DataUpdated();
	}
};

class TupleView : public ItemView {
public:
	TupleView(std::vector<std::unique_ptr<ItemView>> child_list) { Set(std::move(child_list)); }
	TupleView(auto... child) : TupleView([&]() {
		std::vector<std::unique_ptr<ItemView>> child_list; child_list.reserve(sizeof...(child));
		(child_list.emplace_back(std::move(child)), ...);
		return child_list;
	}()) {}
	TupleView(DeserializeContext& context) : descriptor(context.access<std::vector<interpreter_ref>>()) {
		auto size = descriptor.size();
		child_list.reserve(size);
		for (size_t i = 0; i < size; ++i) {
			child_list.emplace_back(ConstructChild(descriptor[i], context));
		}
	}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	std::vector<interpreter_ref> descriptor;
	std::vector<std::unique_ptr<ItemView>> child_list;
private:
	virtual void Serialize(SerializeContext& context) const override {
		context.access(descriptor);
		for (auto& child : child_list) {
			SerializeChild(context, *child);
		}
	}

public:
	void Set(std::vector<std::unique_ptr<ItemView>> child_list) {
		descriptor.clear();
		descriptor.reserve(child_list.size());
		for (size_t i = 0; i < child_list.size(); ++i) {
			RegisterChild(*child_list[i]);
			descriptor.emplace_back(GetChildType(*child_list[i]));
		}
		this->child_list = std::move(child_list);
		DataUpdated();
	}
};

class UnionView : public ItemView {
public:
	UnionView(std::vector<interpreter_ref> descriptor, std::unique_ptr<ItemView> child) { Set(std::move(descriptor), std::move(child)); }
	UnionView(DeserializeContext& context) : descriptor(context.access<std::vector<interpreter_ref>>()), index(context.access<size_t>()), child(ConstructChild(descriptor[index], context)) {}

public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }

private:
	std::vector<interpreter_ref> descriptor;
	size_t index;
	std::unique_ptr<ItemView> child;
private:
	virtual void Serialize(SerializeContext& context) const override {
		context.access(descriptor);
		context.access(index);
		SerializeChild(context, *child);
	}

public:
	void Set(std::vector<interpreter_ref> descriptor, std::unique_ptr<ItemView> child) {
		std::unordered_map<interpreter_ref, size_t> descriptor_index_map;
		for (size_t i = 0; i < descriptor.size(); ++i) {
			auto [it, success] = descriptor_index_map.emplace(descriptor[i], i);
			if (!success) {
				throw std::invalid_argument("repetitive descriptors");
			}
		}
		RegisterChild(*child);
		auto it = descriptor_index_map.find(GetChildType(*child));
		if (it == descriptor_index_map.end()) {
			throw std::invalid_argument("child type not found");
		}
		this->descriptor = std::move(descriptor);
		this->index = it->second;
		this->child = std::move(child);
		DataUpdated();
	}
	void Set(std::unique_ptr<ItemView> child) {
		RegisterChild(*child);
		auto it = std::find(descriptor.begin(), descriptor.end(), GetChildType(*child));
		if (it == descriptor.end()) {
			throw std::invalid_argument("child type not found");
		}
		this->index = it - descriptor.begin();
		this->child = std::move(child);
		DataUpdated();
	}
};


struct DescriptorType;

using descriptor_ref = block<DescriptorType>;

using BasicDescriptor = interpreter_ref;
using ArrayDescriptor = std::tuple<size_t, descriptor_ref>;
struct TupleDescriptor : std::vector<descriptor_ref> { using layout_base = std::vector<descriptor_ref>; };
struct UnionDescriptor : std::vector<descriptor_ref> { using layout_base = std::vector<descriptor_ref>; };

struct DescriptorType : std::variant<BasicDescriptor, ArrayDescriptor, TupleDescriptor, UnionDescriptor> { using layout_base = std::variant<BasicDescriptor, ArrayDescriptor, TupleDescriptor, UnionDescriptor>; };

using DescriptorRegistry = OrderedRefSet<DescriptorType, BlockCacheDynamicAdapter>;
using Descriptor = block_view<DescriptorType, DescriptorRegistry::KeyCache>;


class DescriptorSelect {
private:
	friend class DescriptorAnyView;
	friend class DescriptorView;
private:
	DescriptorSelect(DescriptorRegistry& registry, descriptor_ref ref, DeserializeContext& context) : registry(registry), descriptor(registry.insert(std::move(ref))) {
		view = InstantiateDescriptorView(this->descriptor.get(), context);
	}

private:
	DescriptorRegistry& registry;
	Descriptor descriptor;
private:
	template<class T>
	const T& GetDescriptor() const {
		return std::get<T>(descriptor.get());
	}
	template<class T>
	void SetDescriptor(T descriptor) {
		Descriptor new_type = registry.insert(DescriptorType(std::move(descriptor)));
		if (this->descriptor != new_type) {
			this->descriptor = new_type;
			DescriptorUpdated();
		}
	}

private:
	DescriptorView* parent = nullptr;
	std::unique_ptr<DescriptorView> view;
private:
	std::unique_ptr<DescriptorView> InstantiateDescriptorView(const DescriptorType& type, DeserializeContext& context);

private:
	DescriptorAnyView& AsDescriptorAnyView();
private:
	void DescriptorUpdated();
	void DataUpdated();
private:
	void Serialize(SerializeContext& context) const;
};

class DescriptorAnyView : public ItemView, private DescriptorSelect {
private:
	friend class DescriptorSelect;
private:
	DescriptorAnyView(DescriptorRegistry& registry, descriptor_ref ref, DeserializeContext& context) : DescriptorSelect(registry, std::move(ref), context) {}
public:
	DescriptorAnyView(DeserializeContext& context) : DescriptorAnyView(GetDescriptorRegistry(), context.access<descriptor_ref>(), context) {}
public:
	static const interpreter_ref type;
private:
	virtual interpreter_ref GetType() const override { return type; }
private:
	virtual void Serialize(SerializeContext& context) const override {
		context.access(static_cast<const descriptor_ref&>(descriptor));
		DescriptorSelect::Serialize(context);
	}
public:
	static void ResetDescriptorRegistry(std::unique_ptr<DescriptorRegistry> registry = nullptr);
private:
	static DescriptorRegistry& GetDescriptorRegistry();
};

inline DescriptorAnyView& DescriptorSelect::AsDescriptorAnyView() { return static_cast<DescriptorAnyView&>(*this); }


class DescriptorView {
private:
	friend class DescriptorSelect;
protected:
	DescriptorView(DescriptorSelect& select) : select(select) {}

private:
	DescriptorSelect& select;
protected:
	void RegisterChild(DescriptorSelect& child) { if (child.parent) { throw std::invalid_argument("child already has a parent"); } child.parent = this; }
	void UnregisterChild(DescriptorSelect& child) { VerifyChild(child); child.parent = nullptr; }
	void VerifyChild(const DescriptorSelect& child) const { if (child.parent != this) { throw std::invalid_argument("not a child"); } }

protected:
	DescriptorSelect InstantiateChild(descriptor_ref ref, DeserializeContext& context) { DescriptorSelect child(select.registry, std::move(ref), context); RegisterChild(child); return child; }
protected:
	template<class T> const T& GetDescriptor() const { return select.GetDescriptor<T>(); }
	template<class T> void SetDescriptor(T descriptor) { return select.SetDescriptor<T>(std::move(descriptor)); }
protected:
	descriptor_ref GetChildDescriptorRef(const DescriptorSelect& child) const { VerifyChild(child); return child.descriptor; }

protected:
	void DescriptorUpdated() { select.DescriptorUpdated(); }
	void DataUpdated() { select.DataUpdated(); }
protected:
	virtual void OnChildDescriptorUpdate(DescriptorSelect& child) {}
	virtual void OnChildDataUpdate(DescriptorSelect& child) { DataUpdated(); }
protected:
	void SerializeChild(SerializeContext& context, const DescriptorSelect& child) const { VerifyChild(child); child.Serialize(context); }
protected:
	virtual void Serialize(SerializeContext& context) const {}
};

inline void DescriptorSelect::DescriptorUpdated() { parent ? parent->OnChildDescriptorUpdate(*this) : AsDescriptorAnyView().ItemView::DataUpdated(); }
inline void DescriptorSelect::DataUpdated() { parent ? parent->OnChildDataUpdate(*this) : AsDescriptorAnyView().ItemView::DataUpdated(); }
inline void DescriptorSelect::Serialize(SerializeContext& context) const { view->Serialize(context); }


class BasicDescriptorView : public DescriptorView, private ItemView {
public:
	BasicDescriptorView(DescriptorSelect& select, DeserializeContext& context) : DescriptorView(select) {
		interpreter_ref type = GetDescriptor<BasicDescriptor>();
		item = ConstructChild(type, context);
	}
protected:
	std::unique_ptr<ItemView> item;
private:
	virtual interpreter_ref GetType() const override { return -1; }
private:
	virtual void OnChildDataUpdate(const ItemView& child) { DescriptorView::DataUpdated(); }
	virtual void Serialize(SerializeContext& context) const override { ItemView::SerializeChild(context, *item); }
};

class ArrayDescriptorView : public DescriptorView {
public:
	ArrayDescriptorView(DescriptorSelect& select, DeserializeContext& context) : DescriptorView(select) {
		const auto& [size, descriptor] = GetDescriptor<ArrayDescriptor>();
		child_list.reserve(size);
		for (size_t i = 0; i < size; ++i) {
			child_list.emplace_back(InstantiateChild(descriptor, context));
		}
	}
private:
	std::vector<DescriptorSelect> child_list;
private:
	virtual void OnChildDescriptorUpdate(DescriptorSelect& child) override {
		SetDescriptor(ArrayDescriptor(std::make_tuple(child_list.size(), GetChildDescriptorRef(child))));
	}
private:
	virtual void Serialize(SerializeContext& context) const override {
		for (auto& child : child_list) {
			SerializeChild(context, child);
		}
	}
};

class TupleDescriptorView : public DescriptorView {
public:
	TupleDescriptorView(DescriptorSelect& select, DeserializeContext& context) : DescriptorView(select) {
		const TupleDescriptor& type_list = GetDescriptor<TupleDescriptor>();
		child_list.reserve(type_list.size());
		for (const auto& child : type_list) {
			child_list.emplace_back(InstantiateChild(child, context));
		}
	}
private:
	std::vector<DescriptorSelect> child_list;
private:
	virtual void OnChildDescriptorUpdate(DescriptorSelect& child) override {
		TupleDescriptor descriptor; descriptor.reserve(child_list.size());
		for (auto& child : child_list) {
			descriptor.emplace_back(GetChildDescriptorRef(child));
		}
		SetDescriptor(std::move(descriptor));
	}
private:
	virtual void Serialize(SerializeContext& context) const override {
		for (auto& child : child_list) {
			SerializeChild(context, child);
		}
	}
};

class UnionDescriptorView : public DescriptorView {
public:
	UnionDescriptorView(DescriptorSelect& select, DeserializeContext& context) : DescriptorView(select), index(context.access<size_t>()), child(InstantiateChild(GetDescriptor<UnionDescriptor>()[index], context)) {}
private:
	size_t index;
	DescriptorSelect child;
private:
	virtual void OnChildDescriptorUpdate(DescriptorSelect& child) override {
		UnionDescriptor descriptor = GetDescriptor<UnionDescriptor>();
		descriptor[index] = GetChildDescriptorRef(child);
		SetDescriptor(std::move(descriptor));
	}
private:
	virtual void Serialize(SerializeContext& context) const override {
		context.access(index);
		SerializeChild(context, child);
	}
};


using DescriptorViewMap = TypeMap<
	TypeMapEntry<BasicDescriptor, BasicDescriptorView>,
	TypeMapEntry<ArrayDescriptor, ArrayDescriptorView>,
	TypeMapEntry<TupleDescriptor, TupleDescriptorView>,
	TypeMapEntry<UnionDescriptor, UnionDescriptorView>
>;

inline std::unique_ptr<DescriptorView> DescriptorSelect::InstantiateDescriptorView(const DescriptorType& type, DeserializeContext& context) {
	return std::visit([&](const auto& descriptor) -> std::unique_ptr<DescriptorView> { return std::make_unique<MappedType<DescriptorViewMap, std::remove_cvref_t<decltype(descriptor)>>>(*this, context); }, type);
}


} // namespace Dynamic

} // namespace BlockStore
