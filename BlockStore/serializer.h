#pragma once

#include "block_ref.h"

#include "CppSerialize/layout_traits.h"

#include <array>
#include <bit>
#include <stdexcept>


BEGIN_NAMESPACE(BlockStore)

using CppSerialize::layout_traits;
using CppSerialize::layout_trivial;
using CppSerialize::layout_dynamic;


template<class T>
struct BlockSize {
public:
	BlockSize(const T& object) : object(object) {}
private:
	const T& object;
public:
	static_assert(layout_dynamic<T> || layout_traits<T>::size() <= block_size_limit, "block size exceeds the limit");
public:
	std::pair<size_t, size_t> Get() const {
		size_t size = 0; size_t ref_size = 0;
		access(size, ref_size, object);
		if (size > block_size_limit) { throw std::invalid_argument("block size exceeds the limit"); }
		return std::make_pair(size, ref_size);
	}
private:
	static void access(size_t& size, size_t& ref_size, const layout_trivial auto& object) {
		size += layout_traits<std::remove_cvref_t<decltype(object)>>::size();
	}
	static void access(size_t& size, size_t& ref_size, const block_ref& object) {
		access(size, ref_size, static_cast<index_t>(object));
		ref_size++;
	}
	static void access(size_t& size, size_t& ref_size, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(size, ref_size, item); }, object);
	}
};


template<class T>
struct BlockSerialize {
public:
	BlockSerialize(const T& object) : object(object) {}
private:
	const T& object;
public:
	std::pair<std::vector<std::byte>, std::vector<index_t>> Get() const {
		auto [size, ref_size] = BlockSize(object).Get();
		std::vector<std::byte> data; data.reserve(size);
		std::vector<index_t> ref; ref.reserve(ref_size);
		access(data, ref, object);
		return std::make_pair(data, ref);
	}
private:
	static void access(std::vector<std::byte>& data, std::vector<index_t>& ref, const layout_trivial auto& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(object);
		data.insert(data.end(), bytes.begin(), bytes.end());
	}
	static void access(std::vector<std::byte>& data, std::vector<index_t>& ref, const block_ref& object) {
		access(data, ref, static_cast<index_t>(object));
		ref.push_back(object);
	}
	static void access(std::vector<std::byte>& data, std::vector<index_t>& ref, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(data, ref, item); }, object);
	}
};


template<class T>
struct BlockDeserialize {
public:
	BlockDeserialize(const std::vector<std::byte>& data) : data(data) {}
private:
	const std::vector<std::byte>& data;
public:
	T Get() const {
		block_ref::deserialize_begin();
		T object;
		std::vector<std::byte>::const_iterator index = data.begin();
		access(data, index, object);
		block_ref::deserialize_end();
		return object;
	}
private:
	static void access(const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, layout_trivial auto& object) {
		if (data.end() < index + sizeof(object)) {
			throw std::runtime_error("deserialization error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(index, index + sizeof(object), bytes.begin());
		object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
		index += sizeof(object);
	}
	static void access(const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, block_ref& object) {
		index_t ref;
		access(data, index, ref);
		object = ref;
	}
	static void access(const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::write([&](auto& item) { access(data, index, item); }, object);
	}
};


END_NAMESPACE(BlockStore)