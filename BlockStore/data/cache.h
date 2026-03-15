#pragma once

#include "block.h"
#include "../core/manager.h"

#include <unordered_set>
#include <unordered_map>
#include <any>


namespace BlockStore {

class BlockCache;


template<class T>
class block_view_lazy : public block<T> {
private:
	friend class BlockCache;
private:
	block_view_lazy(block<T> block, BlockCache& cache) : block<T>(std::move(block)), cache(&cache), object(nullptr) {}
	block_view_lazy(block<T> block, BlockCache& cache, const T& object) : block<T>(std::move(block)), cache(&cache), object(&object) {}
public:
	block_view_lazy(const block_view_lazy<T>& other) : block<T>(other), cache(other.cache), object(other.object) { if (object) { cache->inc_ref(*this); } }
	block_view_lazy(block_view_lazy<T>&& other) : block<T>(std::move(other)), cache(other.cache), object(other.object) { other.object = nullptr; }
	~block_view_lazy() { if (object) { cache->dec_ref(*this); } }
private:
	BlockCache* cache;
	mutable const T* object;
public:
	block_view_lazy& operator=(const block<T>& other) { if (object) { cache->dec_ref(*this); object = nullptr; } block<T>::operator=(other); return *this; }
	block_view_lazy& operator=(block_view_lazy<T>&& other) { if (object) { cache->dec_ref(*this); } block<T>::operator=(std::move(other)); cache = other.cache; object = other.object; other.object = nullptr; return *this; }
	block_view_lazy& operator=(const block_view_lazy<T>& other) { if (object) { cache->dec_ref(*this); } block<T>::operator=(other); cache = other.cache; object = other.object; if (object) { cache->inc_ref(*this); } return *this; }
private:
	block<T>::read;
	block<T>::write;
public:
	const T& get() const { if (object == nullptr) { object = &cache->lookup_read(*this); } return *object; }
	const T& get(auto init) const { if (object == nullptr) { object = &cache->lookup_read(*this, std::forward<decltype(init)>(init)); } return *object; }
	const T& set(auto&&... args) { if (object == nullptr) { object = &cache->lookup_write(*this, std::forward<decltype(args)>(args)...); return *object; } else { return cache->update(*this, *object, [&](T& object) { object = T(std::forward<decltype(args)>(args)...); }); } }
	const T& update(auto f) { return cache->update(*this, get(), std::forward<decltype(f)>(f)); }
	const T& update(auto f, auto init) { return cache->update(*this, get(std::forward<decltype(init)>(init)), std::forward<decltype(f)>(f)); }
};


template<class T>
class block_view : public block_view_lazy<T> {
public:
	using block_view_lazy<T>::block_view_lazy;
	block_view(const block_view_lazy<T>& other) : block_view_lazy<T>(other) { block_view_lazy<T>::get(); }
	block_view(const block_view_lazy<T>& other, auto init) : block_view_lazy<T>(other) { block_view_lazy<T>::get(std::forward<decltype(init)>(init)); }
	block_view(block_view_lazy<T>&& other) : block_view_lazy<T>(std::move(other)) { block_view_lazy<T>::get(); }
	block_view(block_view_lazy<T>&& other, auto init) : block_view_lazy<T>(std::move(other)) { block_view_lazy<T>::get(std::forward<decltype(init)>(init)); }
public:
	block_view& operator=(const block<T>& other) { block_view_lazy<T>::operator=(other); block_view_lazy<T>::get(); return *this; }
	block_view& operator=(block_view_lazy<T>&& other) { block_view_lazy<T>::operator=(std::move(other)); block_view_lazy<T>::get(); return *this; }
	block_view& operator=(const block_view_lazy<T>& other) { block_view_lazy<T>::operator=(other); block_view_lazy<T>::get(); return *this; }
};


class BlockCache {
public:
	BlockCache(BlockManager& manager) : manager(manager) {}

private:
	BlockManager& manager;

private:
	using write_fn = void(*)(block_ref&, const std::any&);
	struct Entry {
		block_ref ref;
		size_t count;
		std::any object;
		write_fn write;
	};
private:
	std::unordered_map<ref_t, Entry> map;
private:
	bool has(ref_t ref) { return map.contains(ref); }
	std::any& get(ref_t ref) { auto& entry = map.at(ref); entry.count++; return entry.object; }
	std::any& set(const block_ref& ref, std::any object, write_fn write) { return map.emplace(ref, Entry{ ref, 1, std::move(object), write }).first->second.object; }

private:
	template<class T> friend class block_view_lazy;
private:
	void inc_ref(ref_t ref) { map.at(ref).count++; }
	void dec_ref(ref_t ref) { map.at(ref).count--; }
public:
	void sweep() {
		if (!dirty.empty()) {
			throw std::invalid_argument("changes not written");
		}
		for (auto it = map.begin(); it != map.end();) {
			if (it->second.count == 0) {
				map.erase(it++);
			} else {
				it++;
			}
		}
	}

private:
	std::unordered_set<ref_t> dirty;
private:
	void mark(ref_t ref) { dirty.emplace(ref); }
	void try_commit() {
		for (ref_t ref : dirty) {
			Entry& entry = map.at(ref);
			entry.write(entry.ref, entry.object);
		}
	}
	void end_commit() {
		dirty.clear();
	}

private:
	template<class T>
	T& get(const block_ref& ref) {
		return std::any_cast<T&>(get(ref));
	}
	template<class T>
	T& set(const block_ref& ref, auto&&... args) {
		return std::any_cast<T&>(set(
			ref,
			std::make_any<T>(std::forward<decltype(args)>(args)...),
			[](block_ref& ref, const std::any& object) { static_cast<block<T>&>(ref).write(std::any_cast<const T&>(object)); }
		));
	}

public:
	template<class T>
	const T& lookup_read(const block<T>& block) {
		if (has(block)) {
			return get<T>(block);
		} else {
			return set<T>(block, block.read());
		}
	}
	template<class T>
	const T& lookup_read(const block<T>& block, auto init) {
		if (has(block)) {
			return get<T>(block);
		} else {
			return set<T>(block, block.read(std::forward<decltype(init)>(init)));
		}
	}
	template<class T>
	const T& lookup_write(block<T>& block, auto&&... args) {
		return transaction([&]() -> decltype(auto) {
			if (has(block)) {
				auto& object = get<T>(block);
				object = T(std::forward<decltype(args)>(args)...);
				mark(block);
				return object;
			} else {
				auto& object = set<T>(block, std::forward<decltype(args)>(args)...);
				mark(block);
				return object;
			}
		});
	}

private:
	template<class T>
	const T& update(ref_t ref, const T& object, auto f) {
		return transaction([&]() -> decltype(auto) {
			f(const_cast<T&>(object));
			mark(ref);
			return object;
		});
	}

public:
	template<class T>
	block_view_lazy<T> read_lazy(const block<T>& block) {
		return block_view_lazy<T>(block, *this);
	}
	template<class T>
	block_view<T> read(const block<T>& block) {
		return block_view<T>(block, *this, lookup_read(block));
	}
	template<class T>
	block_view<T> read(const block<T>& block, auto init) {
		return block_view<T>(block, *this, lookup_read(block, std::forward<decltype(init)>(init)));
	}
	template<class T>
	block_view<T> create(auto&&... args) {
		block_ref ref = manager.allocate();
		return block_view<T>(static_cast<const block<T>&>(ref), *this, lookup_write(static_cast<block<T>&>(ref), std::forward<decltype(args)>(args)...));
	}

private:
	size_t transaction_level = 0;
public:
	decltype(auto) transaction(auto f) {
		if constexpr (std::is_void_v<std::invoke_result_t<decltype(f)>>) {
			manager.transaction([&]() {
				transaction_level++;
				f();
				transaction_level--;
				if (transaction_level == 0) {
					try_commit();
				}
			});
			if (transaction_level == 0) {
				end_commit();
			}
		} else {
			decltype(auto) res = manager.transaction([&]() -> decltype(auto) {
				transaction_level++;
				decltype(auto) res = f();
				transaction_level--;
				if (transaction_level == 0) {
					try_commit();
				}
				return res;
			});
			if (transaction_level == 0) {
				end_commit();
			}
			return res;
		}
	}
};


} // namespace BlockStore
