#pragma once

#include "block_ref.h"

#include "CppSerialize/serializer.h"


BEGIN_NAMESPACE(BlockStore)

using CppSerialize::layout_type;
using CppSerialize::layout;
using CppSerialize::declare;
using CppSerialize::layout_size;
using CppSerialize::layout_traits;
using CppSerialize::layout_trivial;
using CppSerialize::layout_static;


template<class T>
struct BlockSize {
public:
	constexpr BlockSize(const T& object) : object(object) {}
private:
	const T& object;
public:
	constexpr std::pair<size_t, size_t> Get() const {
		size_t size = 0; size_t ref_size = 0;
		access(size, ref_size, object);
		return std::make_pair(size, ref_size);
	}
private:
	constexpr static void access(size_t& size, size_t& ref_size, const layout_static auto& object) {
		size += layout_size(layout_type<std::remove_cvref_t<decltype(object)>>());
	}
	constexpr static void access(size_t& size, size_t& ref_size, const block_ref&) {
		static_assert(layout_static<block_ref>);
		size += layout_size(layout_type<block_ref>());
		ref_size++;
	}
	constexpr static void access(size_t& size, size_t& ref_size, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(size, ref_size, item); }, object);
	}
};


template<class T>
struct BlockSerialize {
public:
	constexpr BlockSerialize(const T& object) : object(object) {}
private:
	const T& object;
public:
	constexpr static size_t size_limit = 4096; // byte
public:
	constexpr std::pair<std::vector<std::byte>, std::vector<index_t>> Get() const {
		auto [size, ref_size] = BlockSize(object).Get();
		if (size > size_limit) { throw std::invalid_argument("block size exceeds the limit"); }
		std::vector<std::byte> data; data.reserve(size);
		std::vector<index_t> ref; ref.reserve(ref_size);
		access(data, ref, object);
		return std::make_pair(data, ref);
	}
private:
	constexpr static void access(std::vector<std::byte>& data, std::vector<index_t>& ref, const layout_trivial auto& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(object);
		data.insert(data.end(), bytes.begin(), bytes.end());
	}
	constexpr static void access(std::vector<std::byte>& data, std::vector<index_t>& ref, const block_ref& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(static_cast<index_t>(object));
		data.insert(data.end(), bytes.begin(), bytes.end());
		ref.push_back(object);
	}
	constexpr static void access(std::vector<std::byte>& data, std::vector<index_t>& ref, const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(data, ref, item); }, object);
	}
};


template<class T>
struct BlockDeserialize {
public:
	constexpr BlockDeserialize(const std::vector<std::byte>& data) : data(data) {}
private:
	const std::vector<std::byte>& data;
public:
	constexpr T Get() const {
		block_ref::deserialize_begin();
		T object;
		std::vector<std::byte>::const_iterator index = data.begin();
		access(data, index, object);
		block_ref::deserialize_end();
		return object;
	}
private:
	constexpr static void access(const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, layout_trivial auto& object) {
		if (data.end() < index + sizeof(object)) {
			throw std::runtime_error("deserialization error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(index, index + sizeof(object), bytes.begin());
		object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
		index += sizeof(object);
	}
	constexpr static void access(const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, block_ref& object) {
		if (data.end() < index + sizeof(object)) {
			throw std::runtime_error("deserialization error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(index, index + sizeof(object), bytes.begin());
		object = std::bit_cast<index_t>(bytes);
		index += sizeof(object);
	}
	constexpr static void access(const std::vector<std::byte>& data, std::vector<std::byte>::const_iterator& index, auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::write([&](auto& item) { access(data, index, item); }, object);
	}
};


END_NAMESPACE(BlockStore)