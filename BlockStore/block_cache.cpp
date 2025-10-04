#include "block_cache.h"


BEGIN_NAMESPACE(BlockStore)


size_t block_cache_shared::ObjectCount::count;

std::unordered_map<index_t, std::any> block_cache_shared::map;


END_NAMESPACE(BlockStore)