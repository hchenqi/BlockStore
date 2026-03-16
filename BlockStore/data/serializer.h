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
public:
	std::pair<size_t, size_t> Get() const {
		size_t size = 0; size_t ref_size = 0;
		access(size, ref_size, object);
		if (size > block_size_limit) {
			throw std::invalid_argument("block size exceeds limit");
		}
		return std::make_pair(size, ref_size);
	}
private:
	static void access(size_t& size, size_t& ref_size, const layout_trivial auto& object) {
		size += layout_traits<std::remove_cvref_t<decltype(object)>>::size();
	}
	static void access(size_t& size, size_t& ref_size, const block_ref& object) {
		access(size, ref_size, static_cast<ref_t>(object));
		ref_size++;
	}
	static void access(size_t& size, size_t& ref_size, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(size, ref_size, item); }, object);
	}
};


template<class T>
struct BlockSerialize {
public:
	BlockSerialize(BlockManager& manager, const T& object) : manager(manager), object(object) {}
private:
	BlockManager& manager;
	const T& object;
public:
	std::pair<std::vector<std::byte>, std::vector<ref_t>> Get() const {
		auto [size, ref_size] = BlockSize(object).Get();
		std::vector<std::byte> data; data.reserve(size);
		std::vector<ref_t> ref; ref.reserve(ref_size);
		access(manager, data, ref, object);
		return std::make_pair(data, ref);
	}
private:
	static void access(BlockManager& manager, std::vector<std::byte>& data, std::vector<ref_t>& ref, const layout_trivial auto& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(object);
		data.insert(data.end(), bytes.begin(), bytes.end());
	}
	static void access(BlockManager& manager, std::vector<std::byte>& data, std::vector<ref_t>& ref, const block_ref& object) {
		if (&manager != &object.get_manager()) {
			throw std::invalid_argument("block manager mismatch");
		}
		access(manager, data, ref, static_cast<ref_t>(object));
		ref.push_back(object);
	}
	static void access(BlockManager& manager, std::vector<std::byte>& data, std::vector<ref_t>& ref, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(manager, data, ref, item); }, object);
	}
};


template<class T>
struct BlockDeserialize : protected block_ref_deserialize {
public:
	BlockDeserialize(BlockManager& manager, const std::vector<std::byte>& data) : manager(manager), data(data) {}
private:
	BlockManager& manager;
	const std::vector<std::byte>& data;
public:
	T Get() const {
		T object;
		std::vector<std::byte>::const_iterator index = data.begin();
		access(manager, data, index, object);
		return object;
	}
private:
	static void access(BlockManager& manager, const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, layout_trivial auto& object) {
		if (data.end() < index + sizeof(object)) {
			throw std::runtime_error("deserialization error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(index, index + sizeof(object), bytes.begin());
		object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
		index += sizeof(object);
	}
	static void access(BlockManager& manager, const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, block_ref& object) {
		ref_t ref;
		access(manager, data, index, ref);
		object = block_ref_deserialize::construct(manager, ref);
	}
	static void access(BlockManager& manager, const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::write([&](auto& item) { access(manager, data, index, item); }, object);
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
