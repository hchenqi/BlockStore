#pragma once


#define BEGIN_NAMESPACE(name) namespace name {
#define END_NAMESPACE(name)   }
#define Anonymous

#define ABSTRACT_BASE _declspec(novtable)
#define pure = 0


BEGIN_NAMESPACE(BlockStore)


using uint64 = unsigned long long;
using index_t = uint64;


END_NAMESPACE(BlockStore)