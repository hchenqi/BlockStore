#pragma once

#include "block_ref.h"

#include "CppSerialize/context.h"


BEGIN_NAMESPACE(CppSerialize)

using namespace BlockStore;


static_assert(sizeof(block_ref) == sizeof(index_t));

template<>
constexpr bool has_trivial_layout<block_ref> = true;

END_NAMESPACE(CppSerialize)


BEGIN_NAMESPACE(BlockStore)

using namespace CppSerialize;


struct BlockSizeContext : public SizeContext {
private:
	size_t index_size;
public:
	BlockSizeContext() : SizeContext(), index_size(0) {}
public:
	template<class T> requires has_trivial_layout<T>
	void add(const T&) {
		align_offset<T>(size);
		size += sizeof(T);
	}
	template<class T> requires has_trivial_layout<T>
	void add(const T[], size_t count) {
		align_offset<T>(size);
		size += sizeof(T) * count;
	}
	template<class T>
	void add(const T& obj) {
		Read([&](auto&& ...args) { add(std::forward<decltype(args)>(args)...); }, obj);
	}
	template<>
	void add(const block_ref&) {
		align_offset<index_t>(size);
		size += sizeof(index_t);
		index_size++;
	}
	template<>
	void add(const block_ref[], size_t count) {
		align_offset<index_t>(size);
		size += sizeof(index_t) * count;
		index_size += count;
	}
public:
	size_t GetIndexSize() const { return index_size; }
};


struct BlockSaveContext : public SaveContext {
private:
	index_t* index_curr;
	index_t* index_end;
public:
	BlockSaveContext(byte* begin, size_t length, index_t* index_begin, size_t index_length) :
		SaveContext(begin, length), index_curr(index_begin), index_end(index_begin + index_length) {
	}
private:
	void CheckIndexOffset(const index_t* offset) { if (offset > index_end) { throw std::runtime_error("block save error"); } }
public:
	template<class T> requires has_trivial_layout<T>
	void save(const T& object) {
		align_offset<T>(curr); byte* next = curr + sizeof(T); CheckOffset(next);
		memcpy(curr, &object, sizeof(T)); curr = next;
	}
	template<class T> requires has_trivial_layout<T>
	void save(const T object[], size_t count) {
		align_offset<T>(curr); byte* next = curr + sizeof(T) * count; CheckOffset(next);
		memcpy(curr, object, sizeof(T) * count); curr = next;
	}
	template<class T>
	void save(const T& obj) {
		Read([&](auto&& ...args) { save(std::forward<decltype(args)>(args)...); }, obj);
	}
	template<>
	void save(const block_ref& index) {
		align_offset<index_t>(curr); byte* next = curr + sizeof(index_t); CheckOffset(next);
		memcpy(curr, &index, sizeof(index_t)); curr = next;
		index_t* index_next = index_curr + 1; CheckIndexOffset(index_next);
		memcpy(index_curr, &index, sizeof(index_t)); index_curr = index_next;
	}
	template<>
	void save(const block_ref index[], size_t count) {
		align_offset<index_t>(curr); byte* next = curr + sizeof(index_t) * count; CheckOffset(next);
		memcpy(curr, index, sizeof(index_t) * count); curr = next;
		index_t* index_next = index_curr + count; CheckIndexOffset(index_next);
		memcpy(index_curr, index, sizeof(index_t) * count); index_curr = index_next;
	}
};


using BlockLoadContext = LoadContext;


END_NAMESPACE(BlockStore)