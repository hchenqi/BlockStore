#pragma once

#include "core.h"

#include <vector>


BEGIN_NAMESPACE(BlockStore)


class block_ref : private ObjectCount<block_ref> {
private:
	friend struct block_ref_access;
	template<class T> friend class block;
private:
	index_t index;
private:
	block_ref(index_t index);
	block_ref();
public:
	operator index_t() const { return index; }
protected:
	static void deserialize_begin();
	static void deserialize_end();
protected:
	std::vector<std::byte> read() const;
	void write(std::vector<std::byte> data, std::vector<index_t> ref);
};

template<class T>
constexpr bool is_block_ref = std::is_base_of_v<block_ref, T>;


END_NAMESPACE(BlockStore)