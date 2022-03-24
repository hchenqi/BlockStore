#pragma once

#include "layout.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
constexpr void align_offset(data_t& offset) {
	constexpr data_t alignment = sizeof(T) <= 8 ? sizeof(T) : 8;
	static_assert((alignment & (alignment - 1)) == 0);  // 1, 2, 4, 8
	offset = (offset + (alignment - 1)) & ~(alignment - 1);
}

template<class T>
constexpr void align_offset(const byte*& data) {
	data_t offset = data - (byte*)nullptr; align_offset<T>(offset); data = (byte*)nullptr + offset;
}

template<class T>
constexpr void align_offset(byte*& data) {
	data_t offset = data - (byte*)nullptr; align_offset<T>(offset); data = (byte*)nullptr + offset;
}


struct BlockSizeContext {
private:
	data_t size;
	data_t index_size;
public:
	BlockSizeContext() : size(0), index_size(0) {}
public:
	template<class T> void add(const T&) { align_offset<T>(size); size += sizeof(T); }
	template<class T> void add(T object[], data_t count) { align_offset<T>(size); size += sizeof(T) * count; }
public:
	void add_index() { index_size++; }
public:
	data_t GetSize() const { return size; }
	data_t GetIndexSize() const { return index_size; }
};


struct BlockSaveContext {
private:
	byte* curr;
	byte* end;
	data_t* index_curr;
	data_t* index_end;
public:
	BlockSaveContext(byte* begin, data_t length, data_t* index_begin, data_t index_length) :
		curr(begin), end(begin + length), index_curr(index_begin), index_end(index_begin + index_length) {
	}
private:
	void CheckOffset(const byte* offset) { if (offset > end) { throw std::runtime_error("block save error"); } }
	void CheckIndexOffset(const data_t* offset) { if (offset > index_end) { throw std::runtime_error("block save error"); } }
public:
	template<class T>
	void write(const T& object) {
		align_offset<T>(curr); byte* next = curr + sizeof(T); CheckOffset(next);
		memcpy(curr, &object, sizeof(T)); curr = next;
	}
	template<class T>
	void write(const T object[], data_t count) {
		align_offset<T>(curr); byte* next = curr + sizeof(T) * count; CheckOffset(next);
		memcpy(curr, object, sizeof(T) * count); curr = next;
	}
public:
	void write_index(const data_t& index) {
		data_t* index_next = index_curr + 1; CheckIndexOffset(index_next);
		memcpy(index_curr, &index, sizeof(data_t)); index_curr = index_next;
	}
};


struct BlockLoadContext {
private:
	const byte* curr;
	const byte* end;
	const data_t* index_curr;
	const data_t* index_end;
public:
	BlockLoadContext(const byte* begin, data_t length, const data_t* index_begin, data_t index_length) :
		curr(begin), end(begin + length), index_curr(index_begin), index_end(index_begin + index_length) {
	}
private:
	void CheckOffset(const byte* offset) { if (offset > end) { throw std::runtime_error("block load error"); } }
	void CheckIndexOffset(const data_t* offset) { if (offset > index_end) { throw std::runtime_error("block load error"); } }
public:
	template<class T>
	void read(T& object) {
		align_offset<T>(curr); const byte* next = curr + sizeof(T); CheckOffset(next);
		memcpy(&object, curr, sizeof(T)); curr = next;
	}
	template<class T>
	void read(T object[], data_t count) {
		align_offset<T>(curr); const byte* next = curr + sizeof(T) * count; CheckOffset(next);
		memcpy(object, curr, sizeof(T) * count); curr = next;
	}
public:
	void read_index(data_t& index) {
		const data_t* index_next = index_curr + 1; CheckIndexOffset(index_next);
		memcpy(&index, index_curr, sizeof(data_t)); index_curr = index_next;
	}
};


END_NAMESPACE(BlockStore)