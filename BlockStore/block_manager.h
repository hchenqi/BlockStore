#pragma once

#include "block.h"


BEGIN_NAMESPACE(BlockStore)


class BlockManager {
public:
	static void open_file(const char file[]);
	static block_ref get_root();
	static void collect_garbage();
protected:
	static void begin_transaction();
	static void commit();
	static void rollback();
public:
	static void transaction(auto f) {
		begin_transaction();
		try {
			f();
			commit();
		} catch (...) {
			rollback();
			throw;
		}
	}
};

static constexpr BlockManager block_manager;


END_NAMESPACE(BlockStore)