#pragma once


namespace BlockStore {


template<class Key, class Value>
struct TypeMapEntry {
	using Key = Key;
	using Value = Value;
};

template<class... Entries>
struct TypeMap {};

template<class Map, class Key>
struct Mapped;

template<class Key>
struct Mapped<TypeMap<>, Key> {
	static_assert(sizeof(Key) == 0, "Key not found in TypeMap");
};

template<class Key, class Value, class... Rest>
struct Mapped<TypeMap<TypeMapEntry<Key, Value>, Rest...>, Key> {
	using Type = Value;
};

template<class Head, class... Rest, class Key>
struct Mapped<TypeMap<Head, Rest...>, Key> {
	using Type = typename Mapped<TypeMap<Rest...>, Key>::Type;
};

template<class Map, class Key>
using MappedType = typename Mapped<Map, Key>::Type;


} // namespace BlockStore
