#pragma once

#include "ref.h"
#include "gc.h"

#include <memory>


namespace BlockStore {

class DB;


class BlockManager {
public:
	BlockManager(const char file[]);
	~BlockManager();

private:
	std::unique_ptr<DB> db;
public:
	block_ref get_root();
	block_ref allocate();

private:
	friend class block_ref;
private:
	void inc_ref(ref_t ref);
	void dec_ref(ref_t ref);
private:
	std::vector<std::byte> read(ref_t ref) const;
	void write(ref_t ref, const std::vector<std::byte>& data, const std::vector<ref_t>& ref_list);

	// transaction
protected:
	void begin_transaction();
	void commit();
	void rollback();
public:
	decltype(auto) transaction(auto f) {
		begin_transaction();
		try {
			if constexpr (std::is_void_v<std::invoke_result_t<decltype(f)>>) {
				f();
				commit();
			} else {
				decltype(auto) res = f();
				commit();
				return res;
			}
		} catch (...) {
			rollback();
			throw;
		}
	}

	// gc
public:
	const GCInfo& get_gc_info();
	void gc(const GCOption& option);
};


} // namespace BlockStore
