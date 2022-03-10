#pragma once

#include "block_ref.h"


BEGIN_NAMESPACE(BlockStore)


struct BlockManager {
	static block_ref LoadFile(const char file[]);
	static void SaveFile(block_ref root);
};

static constexpr BlockManager manager;


END_NAMESPACE(BlockStore)