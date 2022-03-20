#pragma once

#include "core.h"

#include <vector>


BEGIN_NAMESPACE(BlockStore)


using block_data = std::pair<std::vector<byte>, std::vector<data_t>>;


END_NAMESPACE(BlockStore)