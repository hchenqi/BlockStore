#pragma once

#include "block.h"

#include <optional>


BEGIN_NAMESPACE(BlockStore)


template<class T>
class block_cache {
private:
	block<T> r;
	T v;
public:
	block_cache(block<T> ref, auto init) : r(ref), v(ref.read(std::move(init))) {}
	block_cache(block<T> ref) : block_cache(ref, []() { return T(); }) {}
public:
	block<T> ref() const { return r; }
	const T& get() const { return v; }
	void set(T&& object) { r.write(v = std::move(object)); }
	void set(const T& object) { r.write(v = object); }
	void update(auto f) { f(v); r.write(v); }
};


template<class T>
class block_cache_lazy {
private:
	block<T> r;
	mutable std::optional<T> v;
public:
	block_cache_lazy(block<T> ref) : r(ref), v() {}
public:
	block<T> ref() const { return r; }
	const T& get(auto init) const { if (!v.has_value()) { v.emplace(r.read(std::move(init))); } return v.value(); }
	const T& get() const { return get([]() { return T(); }); }
	void set(T&& object) { v.emplace(std::move(object)); r.write(v.value()); }
	void set(const T& object) { v.emplace(object); r.write(v.value()); }
	void update(auto f, auto init) { f(const_cast<T&>(get(std::move(init)))); r.write(v.value()); }
	void update(auto f) { update(std::move(f), []() { return T(); }); }
};


END_NAMESPACE(BlockStore)