#include "block_cache.h"


BEGIN_NAMESPACE(BlockStore)


std::unordered_map<index_t, std::any> block_cache_shared_map::map;


END_NAMESPACE(BlockStore)