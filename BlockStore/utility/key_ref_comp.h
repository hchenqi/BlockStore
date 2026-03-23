#pragma once

#include "../data/cache.h"


namespace BlockStore {


template<class Key, template<class T> class Cache>
class KeyRefComp {
private:
	using KeyCache = Cache<Key>;
public:
	KeyRefComp(KeyCache& cache) : cache(cache) {}
private:
	KeyCache& cache;
public:
	bool operator()(const block<Key>& ref, const Key& key) const {
		return cache.read(ref).get() < key;
	}
	bool operator()(const Key& key, const block<Key>& ref) const {
		return key < cache.read(ref).get();
	}
};


} // namespace BlockStore
