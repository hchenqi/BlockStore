#pragma once

#include "layout_context.h"


BEGIN_NAMESPACE(BlockStore)


template<class T, class = void>
struct layout_traits {
	static_assert((layout_type<T>(), false), "block layout undefined");
	static void Size(BlockSizeContext& context, const T& object) {}
	static void Load(BlockLoadContext& context, T& object) {}
	static void Save(BlockSaveContext& context, const T& object) {}
};

template<class T> void Size(BlockSizeContext& context, const T& object) { layout_traits<T>::Size(context, object); }
template<class T> void Load(BlockLoadContext& context, T& object) { layout_traits<T>::Load(context, object); }
template<class T> void Save(BlockSaveContext& context, const T& object) { layout_traits<T>::Save(context, object); }


template<class T>
struct layout_traits<T, std::enable_if_t<has_trivial_layout<T>>> {
	static void Size(BlockSizeContext& context, const T& object) { context.add(object); }
	static void Load(BlockLoadContext& context, T& object) { context.read(object); }
	static void Save(BlockSaveContext& context, const T& object) { context.write(object); }
};


template<class T>
struct layout_traits<T, std::enable_if_t<has_custom_layout<T>>> {
	static void Size(BlockSizeContext& context, const T& object) {
		std::apply([&](auto... member) { (BlockStore::Size(context, object.*member), ...); }, layout(layout_type<T>()));
	}
	static void Load(BlockLoadContext& context, T& object) {
		std::apply([&](auto... member) { (BlockStore::Load(context, object.*member), ...); }, layout(layout_type<T>()));
	}
	static void Save(BlockSaveContext& context, const T& object) {
		std::apply([&](auto... member) { (BlockStore::Save(context, object.*member), ...); }, layout(layout_type<T>()));
	}
};


template<class T1, class T2>
struct layout_traits<std::pair<T1, T2>> {
	static void Size(BlockSizeContext& context, const std::pair<T1, T2>& object) {
		BlockStore::Size(context, object.first); BlockStore::Size(context, object.second);
	}
	static void Load(BlockLoadContext& context, std::pair<T1, T2>& object) {
		BlockStore::Load(context, object.first); BlockStore::Load(context, object.second);
	}
	static void Save(BlockSaveContext& context, const std::pair<T1, T2>& object) {
		BlockStore::Save(context, object.first); BlockStore::Save(context, object.second);
	}
};


template<class... Ts>
struct layout_traits<std::tuple<Ts...>> {
	static void Size(BlockSizeContext& context, const std::tuple<Ts...>& object) {
		std::apply([&](auto&... member) { (BlockStore::Size(context, member), ...); }, object);
	}
	static void Load(BlockLoadContext& context, std::tuple<Ts...>& object) {
		std::apply([&](auto&... member) { (BlockStore::Load(context, member), ...); }, object);
	}
	static void Save(BlockSaveContext& context, const std::tuple<Ts...>& object) {
		std::apply([&](auto&... member) { (BlockStore::Save(context, member), ...); }, object);
	}
};


END_NAMESPACE(BlockStore)