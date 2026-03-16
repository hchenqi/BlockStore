#pragma once

#include "type.h"

#include <vector>


namespace BlockStore {

class BlockManager;


class block_ref {
private:
	friend class BlockManager;
	friend class block_ref_deserialize;
private:
	BlockManager* manager;
	ref_t ref;
public:
	block_ref();
private:
	block_ref(BlockManager& manager, ref_t ref);
public:
	block_ref(block_ref&& other);
	block_ref(const block_ref& other);
	block_ref& operator=(block_ref&& other);
	block_ref& operator=(const block_ref& other);
	~block_ref();
private:
	void check() const;
public:
	BlockManager& get_manager() const { check(); return *manager; }
	operator ref_t() const { check(); return ref; }
public:
	std::vector<std::byte> read() const;
	void write(const std::vector<std::byte>& data, const std::vector<ref_t>& ref_list);
};


class block_ref_deserialize {
protected:
	static block_ref construct(BlockManager& manager, ref_t ref) { return block_ref(manager, ref); }
};


} // namespace BlockStore
