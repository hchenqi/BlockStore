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
	template<class T> requires (is_layout_trivial<T> && !is_block_ref<T>)
	void add(const T&) {
		size += layout_traits<T>::size();
	}
	template<class T> requires (is_layout_trivial<T> && !is_block_ref<T>)
	void add(const T[], size_t count) {
		size += layout_traits<T>::size() * count;
	}
	template<class T> requires (!is_layout_trivial<T> && !is_block_ref<T>)
	void add(const T& obj) {
		layout_traits<T>::read([&](auto&& ...args) { add(std::forward<decltype(args)>(args)...); }, obj);
	}
	void add(const block_ref&) {
		size += sizeof(index_t);
		index_size++;
	}
	void add(const block_ref[], size_t count) {
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
	template<class T> requires (is_layout_trivial<T> && !is_block_ref<T>)
	void save(const T& object) {
		byte* next = curr + sizeof(T); CheckOffset(next);
		memcpy(curr, &object, sizeof(T)); curr = next;
	}
	template<class T> requires (is_layout_trivial<T> && !is_block_ref<T>)
	void save(const T object[], size_t count) {
		byte* next = curr + sizeof(T) * count; CheckOffset(next);
		memcpy(curr, object, sizeof(T) * count); curr = next;
	}
	template<class T> requires (!is_layout_trivial<T> && !is_block_ref<T>)
	void save(const T& obj) {
		layout_traits<T>::read([&](auto&& ...args) { save(std::forward<decltype(args)>(args)...); }, obj);
	}
	void save(const block_ref& index) {
		byte* next = curr + sizeof(index_t); CheckOffset(next);
		memcpy(curr, &index, sizeof(index_t)); curr = next;
		index_t* index_next = index_curr + 1; CheckIndexOffset(index_next);
		memcpy(index_curr, &index, sizeof(index_t)); index_curr = index_next;
	}
	void save(const block_ref index[], size_t count) {
		byte* next = curr + sizeof(index_t) * count; CheckOffset(next);
		memcpy(curr, index, sizeof(index_t) * count); curr = next;
		index_t* index_next = index_curr + count; CheckIndexOffset(index_next);
		memcpy(index_curr, index, sizeof(index_t) * count); index_curr = index_next;
	}
};


using BlockLoadContext = LoadContext;


END_NAMESPACE(BlockStore)