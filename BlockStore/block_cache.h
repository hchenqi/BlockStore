#pragma once

#include "block.h"

#include <unordered_map>
#include <any>


BEGIN_NAMESPACE(BlockStore)


struct block_cache_shared_map {
private:
	static std::unordered_map<index_t, std::any> map;
protected:
	static bool has(block_ref ref) {
		return map.contains(ref);
	}
	template<class T>
	static T& get(block_ref ref) {
		return std::any_cast<T&>(map.at(ref));
	}
	template<class T, class... Args>
	static T& set(block_ref ref, Args&&... args) {
		return map[ref].emplace<T>(std::forward<Args>(args)...);
	}
protected:
	template<class T>
	static T& lookup(block<T> ref, auto init) {
		if (block_cache_shared_map::has(ref)) {
			return block_cache_shared_map::get<T>(ref);
		} else {
			return block_cache_shared_map::set<T>(ref, ref.read(std::move(init)));
		}
	}
};


template<class T>
class block_cache : private block_cache_shared_map {
private:
	block<T> r;
	std::reference_wrapper<T> v;
public:
	block_cache(block<T> ref, auto init) : r(ref), v(lookup(ref, std::move(init))) {}
	block_cache(block<T> ref) : block_cache(ref, []() { return T(); }) {}
public:
	block<T> ref() const { return r; }
	const T& get() const { return v; }
	void set(T&& object) { r.write(v.get() = std::move(object)); }
	void set(const T& object) { r.write(v.get() = object); }
	void update(auto f) { f(v); r.write(v); }
};


template<class T>
class block_cache_lazy : private block_cache_shared_map {
private:
	block<T> r;
	mutable T* v;
public:
	block_cache_lazy(block<T> ref) : r(ref), v(nullptr) {}
public:
	block<T> ref() const { return r; }
	const T& get(auto init) const { if (v == nullptr) { v = &lookup(r, std::move(init)); } return *v; }
	const T& get() const { return get([]() { return T(); }); }
	template<class... Args>
	void set(Args&&... args) { v = &block_cache_shared_map::set(r, std::forward<Args>(args)...); r.write(*v); }
	void update(auto f, auto init) { f(const_cast<T&>(get(std::move(init)))); r.write(*v); }
	void update(auto f) { update(std::move(f), []() { return T(); }); }
};


END_NAMESPACE(BlockStore)