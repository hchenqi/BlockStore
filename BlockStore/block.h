#pragma once

#include "core.h"
#include "CppSerialize/cpp_serialize.h"
#include "CppSerialize/layout_traits_stl.h"

#include <functional>


BEGIN_NAMESPACE(BlockStore)

using CppSerialize::byte;
using CppSerialize::Serialize;
using CppSerialize::Deserialize;


struct block_ref {
private:
	friend class block;
	friend class BlockManager;
private:
	index_t index;
private:
	block_ref(index_t index) : index(index) {}
	operator index_t() const { return index; }
public:
	block_ref() : index(block_index_invalid) {}
};


class block {
private:
	index_t index;

public:
	block() : index(block_index_invalid) {}
	block(block_ref ref) : index(ref.index) {}

public:
	bool empty() const { return index == block_index_invalid; }
	operator block_ref() const { return index; }

public:
	std::pair<std::vector<byte>, std::vector<block_ref>> read();
	void write(std::vector<byte> data, std::vector<block_ref> ref_list);
};


END_NAMESPACE(BlockStore)