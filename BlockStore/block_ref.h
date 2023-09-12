#pragma once

#include "core.h"


BEGIN_NAMESPACE(BlockStore)


struct block_ref {
private:
	friend class BlockManager;
	template<class T> friend class block;
private:
	index_t index;
private:
	block_ref(index_t index) : index(index) {}
	operator index_t() const { return index; }
public:
	block_ref() : index(block_index_invalid) {}
};

static_assert(sizeof(block_ref) == sizeof(index_t));


END_NAMESPACE(BlockStore)