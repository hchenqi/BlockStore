#pragma once


namespace BlockStore {


template<class Key, class Value>
struct TypeMapEntry {};

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


template<class Map, class Key, class Default>
struct MappedOr;

template<class Key, class Default>
struct MappedOr<TypeMap<>, Key, Default> {
	using Type = Default;
};

template<class Key, class Value, class... Rest, class Default>
struct MappedOr<TypeMap<TypeMapEntry<Key, Value>, Rest...>, Key, Default> {
	using Type = Value;
};

template<class Head, class... Rest, class Key, class Default>
struct MappedOr<TypeMap<Head, Rest...>, Key, Default> {
	using Type = typename MappedOr<TypeMap<Rest...>, Key, Default>::Type;
};

template<class Map, class Key, class Default>
using MappedTypeOr = typename MappedOr<Map, Key, Default>::Type;


} // namespace BlockStore
