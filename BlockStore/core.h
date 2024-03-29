#pragma once

#include <stdexcept>
#include <cassert>


#define BEGIN_NAMESPACE(name) namespace name {
#define END_NAMESPACE(name)   }
#define Anonymous

#define ABSTRACT_BASE _declspec(novtable)
#define pure = 0


BEGIN_NAMESPACE(BlockStore)


using uint64 = unsigned long long;
using index_t = uint64;
constexpr index_t block_index_invalid = -1;


END_NAMESPACE(BlockStore)