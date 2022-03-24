#pragma once

#include "block_ref.h"


BEGIN_NAMESPACE(BlockStore)


struct BlockManager {
	static void open(const char file[]);
	static block_ref get_root();
	static void set_root(const block_ref& root);
	static void close();
};

static constexpr BlockManager block_manager;


END_NAMESPACE(BlockStore)