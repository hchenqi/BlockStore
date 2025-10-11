#pragma once

#include "block.h"

#include <unordered_map>
#include <any>


BEGIN_NAMESPACE(BlockStore)


struct block_cache_shared : private ObjectCount<block_cache_shared> {
private:
	static std::unordered_map<index_t, std::any> map;
private:
	static bool has(const block_ref& ref) {
		return map.contains(ref);
	}
	template<class T>
	static T& get(const block_ref& ref) {
		return std::any_cast<T&>(map.at(ref));
	}
	template<class T>
	static T& set(const block_ref& ref, auto&&... args) {
		return map[ref].emplace<T>(std::forward<decltype(args)>(args)...);
	}
protected:
	template<class T>
	static const T& lookup_read(const block<T>& ref, auto init) {
		if (has(ref)) {
			return get<T>(ref);
		} else {
			return set<T>(ref, ref.read(std::move(init)));
		}
	}
	template<class T>
	static const T& lookup_write(block<T>& ref, auto&&... args) {
		if (has(ref)) {
			auto& object = get<T>(ref);
			object = T(std::forward<decltype(args)>(args)...);
			ref.write(object);
			return object;
		} else {
			auto& object = set<T>(ref, std::forward<decltype(args)>(args)...);
			ref.write(object);
			return object;
		}
	}
	template<class T>
	static const T& update(block<T>& ref, const T& object, auto f) {
		f(const_cast<T&>(object));
		ref.write(object);
		return object;
	}
public:
	static void clear() {
		if (GetCount() > 0) {
			throw std::invalid_argument("cannot clear block cache with active instances");
		}
		map.clear();
	}

	// transaction copy back on exception
	//static auto transaction(auto f) {}
};


template <class T>
class block_cache_lazy;

template<class T>
class block_cache : public block<T>, private block_cache_shared {
private:
	friend class block_cache_lazy<T>;
private:
	std::reference_wrapper<const T> v;
public:
	block_cache(const block<T>& ref, auto init) : block<T>(ref), v(block_cache_shared::lookup_read(*this, std::move(init))) {}
	block_cache(const block<T>& ref) : block_cache(ref, []() { return T(); }) {}
	block_cache(std::in_place_t, auto&&... args) : block<T>(), v(block_cache_shared::lookup_write(*this, std::forward<decltype(args)>(args)...)) {}
public:
	const T& get() const { return v; }
	const T& set(auto&&... args) { return update([&](T& object) { object = T(std::forward<decltype(args)>(args)...); }); }
	const T& update(auto f) { return block_cache_shared::update(*this, get(), std::move(f)); }
};


template<class T>
class block_cache_lazy : public block<T>, private block_cache_shared {
private:
	mutable const T* v;
public:
	block_cache_lazy() : block<T>(), v(nullptr) {}
	block_cache_lazy(const block<T>& ref) : block<T>(ref), v(nullptr) {}
	block_cache_lazy(const block_cache<T>& cache) : block<T>(cache), v(&cache.get()) {}
public:
	const T& get(auto init) const { if (v == nullptr) { v = &block_cache_shared::lookup_read(*this, std::move(init)); } return *v; }
	const T& get() const { return get([]() { return T(); }); }
	const T& set(auto&&... args) { v = &block_cache_shared::lookup_write(*this, std::forward<decltype(args)>(args)...); return *v; }
	const T& update(auto f, auto init) { return block_cache_shared::update(*this, get(std::move(init)), std::move(f)); }
	const T& update(auto f) { return update(std::move(f), []() { return T(); }); }
};


END_NAMESPACE(BlockStore)