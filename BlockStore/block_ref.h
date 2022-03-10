#pragma once

#include "core.h"

#include <memory>


BEGIN_NAMESPACE(BlockStore)


struct block_ref {
public:
	static constexpr data_t file_index_block_cache = 0;
	static constexpr data_t block_index_invalid = -1;
private:
	data_t file_index;
	data_t block_index;
public:
	block_ref(data_t file_index, data_t block_index) : file_index(file_index), block_index(block_index) {}
	block_ref() : file_index(file_index_block_cache), block_index(block_index_invalid) {}
public:
	template<class T> const T& read() {}
	template<class T> T& write() {}
};


END_NAMESPACE(BlockStore)