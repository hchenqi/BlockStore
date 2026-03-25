#pragma once

#include "../core/ref.h"

#include "CppSerialize/layout_traits.h"

#include <array>
#include <bit>
#include <stdexcept>


namespace BlockStore {

using CppSerialize::layout_traits;
using CppSerialize::layout_trivial;
using CppSerialize::layout_dynamic;

constexpr size_t block_size_limit = 4096; // byte


template<class T>
struct BlockSize {
public:
	BlockSize(const T& object) : object(object) {}
private:
	const T& object;
public:
	static_assert(layout_dynamic<T> || layout_traits<T>::size() <= block_size_limit, "block size exceeds limit");
private:
	struct Context {
		size_t size;
		size_t ref_size;
	};
public:
	std::pair<size_t, size_t> Get() const {
		Context context = { 0, 0 };
		access(context, object);
		if (context.size > block_size_limit) {
			throw std::invalid_argument("block size exceeds limit");
		}
		return std::make_pair(context.size, context.ref_size);
	}
private:
	static void access(Context& context, const layout_trivial auto& object) {
		context.size += layout_traits<std::remove_cvref_t<decltype(object)>>::size();
	}
	static void access(Context& context, const block_ref& object) {
		access(context, static_cast<ref_t>(object));
		context.ref_size++;
	}
	static void access(Context& context, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(context, item); }, object);
	}
};


template<class T>
struct BlockSerialize {
public:
	BlockSerialize(BlockManager& manager, const T& object) : manager(manager), object(object) {}
private:
	BlockManager& manager;
	const T& object;
private:
	struct Context {
		BlockManager& manager;
		std::vector<std::byte> data;
		std::vector<ref_t> ref;
	};
public:
	std::pair<std::vector<std::byte>, std::vector<ref_t>> Get() const {
		Context context = { manager };
		auto [size, ref_size] = BlockSize(object).Get();
		context.data.reserve(size); context.ref.reserve(ref_size);
		access(context, object);
		return std::make_pair(std::move(context.data), std::move(context.ref));
	}
private:
	static void access(Context& context, const layout_trivial auto& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(object);
		context.data.insert(context.data.end(), bytes.begin(), bytes.end());
	}
	static void access(Context& context, const block_ref& object) {
		if (&context.manager != &object.get_manager()) {
			throw std::invalid_argument("block manager mismatch");
		}
		access(context, static_cast<ref_t>(object));
		context.ref.push_back(object);
	}
	static void access(Context& context, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(context, item); }, object);
	}
};


template<class T>
struct BlockDeserialize : protected block_ref_deserialize {
public:
	BlockDeserialize(BlockManager& manager, const std::vector<std::byte>& data) : manager(manager), data(data) {}
private:
	BlockManager& manager;
	const std::vector<std::byte>& data;
private:
	struct Context {
		BlockManager& manager;
		const std::vector<std::byte>& data;
		std::vector<std::byte>::const_iterator index;
	};
public:
	T Get() const {
		T object;
		Context context = { manager, data, data.begin() };
		access(context, object);
		return object;
	}
private:
	static void access(Context& context, layout_trivial auto& object) {
		if (context.data.end() < context.index + sizeof(object)) {
			throw std::runtime_error("deserialization error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(context.index, context.index + sizeof(object), bytes.begin());
		object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
		context.index += sizeof(object);
	}
	static void access(Context& context, block_ref& object) {
		ref_t ref;
		access(context, ref);
		object = block_ref_deserialize::construct(context.manager, ref);
	}
	static void access(Context& context, auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::write([&](auto& item) { access(context, item); }, object);
	}
};


} // namespace BlockStore


namespace CppSerialize {

template<>
struct layout_traits<BlockStore::block_ref> {
	constexpr static layout_size size() { return { sizeof(BlockStore::ref_t) }; }
	constexpr static void read(auto f, const BlockStore::block_ref& object) { f(object); }
	constexpr static void write(auto f, BlockStore::block_ref& object) { f(object); }
};

} // namespace CppSerialize
