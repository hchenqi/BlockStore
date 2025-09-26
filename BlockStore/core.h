#pragma once


#define BEGIN_NAMESPACE(name) namespace name {
#define END_NAMESPACE(name)   }
#define Anonymous

#define ABSTRACT_BASE _declspec(novtable)
#define pure = 0


BEGIN_NAMESPACE(BlockStore)


using uint64 = unsigned long long;
using index_t = uint64;


template<class T>
class ObjectCount {
public:
	ObjectCount() { count++; }
	ObjectCount(const ObjectCount&) { count++; }
	~ObjectCount() { count--; }
private:
	static size_t count;
public:
	static size_t GetCount() { return count; }
};


END_NAMESPACE(BlockStore)