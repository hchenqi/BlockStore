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
public:
	BlockSizeContext() : size(0) {}
public:
	template<class T> void add(const T&) { align_offset<T>(size); size += sizeof(T); }
	template<class T> void add(T object[], data_t count) { align_offset<T>(size); size += sizeof(T) * count; }
public:
	data_t GetSize() const { return size; }
};


struct BlockLoadContext {
private:
	const byte* curr;
	const byte* end;
public:
	BlockLoadContext(const byte* begin, data_t length) : curr(begin), end(begin + length) {}
private:
	void CheckOffset(const byte* offset) { if (offset > end) { throw std::runtime_error("block size mismatch"); } }
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
};


struct BlockSaveContext {
private:
	byte* curr;
	byte* end;
public:
	BlockSaveContext(byte* begin, data_t length) : curr(begin), end(begin + length) {}
private:
	void CheckOffset(const byte* offset) { if (offset > end) { throw std::runtime_error("block size mismatch"); } }
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
};


END_NAMESPACE(BlockStore)