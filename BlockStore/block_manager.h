#pragma once

#include "block.h"

#include <functional>


BEGIN_NAMESPACE(BlockStore)


class BlockManager {
public:
	static void open_file(const char file[]);
	static block_ref get_root();
	static void transaction(std::function<void(void)> op);
	static void collect_garbage();
};

static constexpr BlockManager block_manager;


END_NAMESPACE(BlockStore)