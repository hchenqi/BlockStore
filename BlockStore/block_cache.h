#pragma once

#include "block.h"

#include <unordered_map>
#include <any>


BEGIN_NAMESPACE(BlockStore)


struct block_cache_shared_map {
private:
	static std::unordered_map<index_t, std::any> map;
protected:
	static bool has(const block_ref& ref) {
		return map.contains(ref);
	}
	template<class T>
	static T& get(const block_ref& ref) {
		return std::any_cast<T&>(map.at(ref));
	}
	template<class T, class... Args>
	static T& set(const block_ref& ref, Args&&... args) {
		return map[ref].emplace<T>(std::forward<Args>(args)...);
	}
protected:
	template<class T>
	static T& lookup(const block<T>& ref, auto init) {
		if (block_cache_shared_map::has(ref)) {
			return block_cache_shared_map::get<T>(ref);
		} else {
			return block_cache_shared_map::set<T>(ref, ref.read(std::move(init)));
		}
	}
};


template <class T>
class block_cache_lazy;

template<class T>
class block_cache : public block<T>, private block_cache_shared_map {
private:
	friend class block_cache_lazy<T>;
private:
	std::reference_wrapper<const T> v;
public:
	block_cache(const block<T>& ref, auto init) : block<T>(ref), v(block_cache_shared_map::lookup(*this, std::move(init))) {}
	block_cache(const block<T>& ref) : block_cache(ref, []() { return T(); }) {}
	template<class... Args> requires std::constructible_from<T, Args...>
	block_cache(std::in_place_t, Args&&... args) : block<T>(), v(set(std::forward<Args>(args)...)) {}
public:
	const T& get() const { return v; }
	template<class... Args>
	const T& set(Args&&... args) { v = block_cache_shared_map::set<T>(*this, std::forward<Args>(args)...); block<T>::write(v); return v; }
	const T& update(auto f) { f(const_cast<T&>(get())); block<T>::write(v); return v; }
};


template<class T>
class block_cache_lazy : public block<T>, private block_cache_shared_map {
private:
	mutable const T* v;
public:
	block_cache_lazy(const block<T>& ref) : block<T>(ref), v(nullptr) {}
	block_cache_lazy(const block_cache<T>& cache) : block<T>(cache), v(&cache.v.get()) {}
public:
	const T& get(auto init) const { if (v == nullptr) { v = &block_cache_shared_map::lookup(*this, std::move(init)); } return *v; }
	const T& get() const { return get([]() { return T(); }); }
	template<class... Args>
	const T& set(Args&&... args) { v = &block_cache_shared_map::set<T>(*this, std::forward<Args>(args)...); block<T>::write(*v); return *v; }
	const T& update(auto f, auto init) { f(const_cast<T&>(get(std::move(init)))); block<T>::write(*v); return *v; }
	const T& update(auto f) { return update(std::move(f), []() { return T(); }); }
};


END_NAMESPACE(BlockStore)