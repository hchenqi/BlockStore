#pragma once

#include "block_manager.h"

#include <any>


BEGIN_NAMESPACE(BlockStore)


struct block_cache_shared : private ObjectCount<block_cache_shared> {
private:
	using write_fn = void(*)(const block_ref&, const std::any&);
	using map_value = std::tuple<std::any, write_fn>;
private:
	static bool has(index_t index);
	static std::any& get(index_t index);
	static std::any& set(index_t index, map_value value);
	static void mark(index_t index);
public:
	static void clear();

private:
	template<class T>
	static T& get(const block_ref& ref) {
		return std::any_cast<T&>(get(ref));
	}
	template<class T>
	static T& set(const block_ref& ref, auto&&... args) {
		return std::any_cast<T&>(set(ref, std::make_tuple(
			std::make_any<T>(std::forward<decltype(args)>(args)...),
			[](const block_ref& ref, const std::any& object) { block<T>(ref).write(std::any_cast<const T&>(object)); }
		)));
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
		return block_manager.transaction([&]() -> decltype(auto) {
			if (has(ref)) {
				auto& object = get<T>(ref);
				object = T(std::forward<decltype(args)>(args)...);
				mark(ref);
				return object;
			} else {
				auto& object = set<T>(ref, std::forward<decltype(args)>(args)...);
				mark(ref);
				return object;
			}
		});
	}
	template<class T>
	static const T& update(block<T>& ref, const T& object, auto f) {
		return block_manager.transaction([&]() -> decltype(auto) {
			f(const_cast<T&>(object));
			mark(ref);
			return object;
		});
	}
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
private:
	block<T>::read;
	block<T>::write;
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
private:
	block<T>::read;
	block<T>::write;
public:
	const T& get(auto init) const { if (v == nullptr) { v = &block_cache_shared::lookup_read(*this, std::move(init)); } return *v; }
	const T& get() const { return get([]() { return T(); }); }
	const T& set(auto&&... args) { v = &block_cache_shared::lookup_write(*this, std::forward<decltype(args)>(args)...); return *v; }
	const T& update(auto f, auto init) { return block_cache_shared::update(*this, get(std::move(init)), std::move(f)); }
	const T& update(auto f) { return update(std::move(f), []() { return T(); }); }
};


END_NAMESPACE(BlockStore)