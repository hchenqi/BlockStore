#pragma once

#include "block.h"


BEGIN_NAMESPACE(BlockStore)


class BlockManager {
public:
	static void open_file(const char file[]);
	static block_ref get_root();
	static void collect_garbage();
};

static constexpr BlockManager block_manager;


END_NAMESPACE(BlockStore)