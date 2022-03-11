#pragma once

#include <stdexcept>
#include <cassert>


#define BEGIN_NAMESPACE(name) namespace name {
#define END_NAMESPACE(name)   }
#define Anonymous

#define ABSTRACT_BASE _declspec(novtable)
#define pure = 0


BEGIN_NAMESPACE(BlockStore)


template<class T> using ref_ptr = T*;
template<class T> using alloc_ptr = T*;

using uchar = unsigned char;
using ushort = unsigned short;
using uint = unsigned int;
using uint64 = unsigned long long;
using int64 = long long;
using wchar = wchar_t;
using byte = std::byte;

using data_t = uint64;


END_NAMESPACE(BlockStore)