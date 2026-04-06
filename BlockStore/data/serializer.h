#pragma once

#include "../core/ref.h"

#include "CppSerialize/layout_traits.h"

#include <array>
#include <bit>
#include <stdexcept>


namespace BlockStore {

using CppSerialize::layout_traits;
using CppSerialize::layout_trivial;


struct SizeContext {
public:
	SizeContext() {}
private:
	size_t size = 0;
	size_t ref_size = 0;
public:
	std::pair<size_t, size_t> Get() {
		return std::make_pair(size, ref_size);
	}
public:
	SizeContext& access(const layout_trivial auto& object) {
		size += layout_traits<std::remove_cvref_t<decltype(object)>>::size();
		return *this;
	}
	SizeContext& access(const block_ref& object) {
		access(static_cast<ref_t>(object));
		ref_size++;
		return *this;
	}
	SizeContext& access(const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(item); }, object);
		return *this;
	}
};


struct SerializeContext {
public:
	SerializeContext(BlockManager& manager) : manager(manager) {}
private:
	BlockManager& manager;
	std::vector<std::byte> data;
	std::vector<ref_t> ref_list;
public:
	std::pair<std::vector<std::byte>, std::vector<ref_t>> Get() {
		return std::make_pair(std::move(data), std::move(ref_list));
	}
public:
	SerializeContext& access(const layout_trivial auto& object) {
		auto bytes = std::bit_cast<std::array<std::byte, sizeof(object)>>(object);
		data.insert(data.end(), bytes.begin(), bytes.end());
		return *this;
	}
	SerializeContext& access(const block_ref& object) {
		if (&manager != &object.get_manager()) {
			throw std::invalid_argument("block manager mismatch");
		}
		access(static_cast<ref_t>(object));
		ref_list.push_back(object);
		return *this;
	}
	SerializeContext& access(const auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::read([&](const auto& item) { access(item); }, object);
		return *this;
	}
};


struct DeserializeContext : protected block_ref_deserialize {
public:
	DeserializeContext(BlockManager& manager, std::vector<std::byte> data) : manager(manager), data(std::move(data)), index(this->data.begin()) {}
private:
	BlockManager& manager;
	std::vector<std::byte> data;
	std::vector<std::byte>::const_iterator index;
public:
	template<class T>
	T access() {
		T object;
		access(object);
		return object;
	}
	DeserializeContext& access(layout_trivial auto& object) {
		if (data.end() < index + sizeof(object)) {
			throw std::runtime_error("deserialization error");
		}
		std::array<std::byte, sizeof(object)> bytes;
		std::copy(index, index + sizeof(object), bytes.begin());
		object = std::bit_cast<std::remove_cvref_t<decltype(object)>>(bytes);
		index += sizeof(object);
		return *this;
	}
	DeserializeContext& access(block_ref& object) {
		ref_t ref;
		access(ref);
		object = block_ref_deserialize::construct(manager, ref);
		return *this;
	}
	DeserializeContext& access(auto& object) {
		layout_traits<std::remove_cvref_t<decltype(object)>>::write([&](auto& item) { access(item); }, object);
		return *this;
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
