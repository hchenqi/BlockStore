#pragma once

#include "core.h"

#include "CppSerialize/layout_traits.h"

#include <vector>


BEGIN_NAMESPACE(BlockStore)


class block_ref : private ObjectCount<block_ref> {
private:
	friend struct block_ref_access;
	template<class T> friend class block;
	template<class T> friend class BlockDeserialize;
private:
	index_t index;
private:
	block_ref(index_t index);
	block_ref();
public:
	operator index_t() const { return index; }
private:
	static void deserialize_begin();
	static void deserialize_end();
private:
	std::vector<std::byte> read() const;
	void write(const std::vector<std::byte>& data, const std::vector<index_t>& ref);
};


END_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(CppSerialize)


template<>
struct layout_traits<BlockStore::block_ref> : layout_traits_trivial<BlockStore::block_ref> {};

static_assert(layout_traits<BlockStore::block_ref>::size() == sizeof(BlockStore::index_t));


END_NAMESPACE(CppSerialize)