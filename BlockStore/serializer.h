#pragma once

#include "block_ref.h"

#include "CppSerialize/context.h"


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
		index_size++;
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
		index_t* index_next = index_curr + 1; CheckIndexOffset(index_next);
		memcpy(index_curr, &index, sizeof(index_t)); index_curr = index_next;
	}
};


struct BlockLoadContext : public LoadContext {
private:
	const index_t* index_curr;
	const index_t* index_end;
public:
	BlockLoadContext(const byte* begin, size_t length, const index_t* index_begin, size_t index_length) :
		LoadContext(begin, length), index_curr(index_begin), index_end(index_begin + index_length) {
	}
private:
	void CheckIndexOffset(const index_t* offset) { if (offset > index_end) { throw std::runtime_error("block load error"); } }
public:
	template<class T> requires has_trivial_layout<T>
	void load(T& object) {
		align_offset<T>(curr); const byte* next = curr + sizeof(T); CheckOffset(next);
		memcpy(&object, curr, sizeof(T)); curr = next;
	}
	template<class T> requires has_trivial_layout<T>
	void load(T object[], size_t count) {
		align_offset<T>(curr); const byte* next = curr + sizeof(T) * count; CheckOffset(next);
		memcpy(object, curr, sizeof(T) * count); curr = next;
	}
	template<class T>
	void load(T& obj) {
		Write([&](auto&& ...args) { load(std::forward<decltype(args)>(args)...); }, obj);
	}
	template<>
	void load(block_ref& index) {
		const index_t* index_next = index_curr + 1; CheckIndexOffset(index_next);
		memcpy(&index, index_curr, sizeof(index_t)); index_curr = index_next;
	}
};


END_NAMESPACE(BlockStore)