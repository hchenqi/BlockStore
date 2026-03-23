#pragma once

#include "block.h"
#include "../core/manager.h"

#include <unordered_map>
#include <unordered_set>
#include <any>
#include <optional>


namespace BlockStore {


template<class T, class CacheType>
class block_view_lazy : public block<T> {
private:
	friend CacheType;
private:
	block_view_lazy(block<T> ref, CacheType& cache) : block<T>(std::move(ref)), cache(&cache), object(nullptr) {}
	block_view_lazy(block<T> ref, CacheType& cache, std::in_place_t, auto&&... args) : block<T>(std::move(ref)), cache(&cache), object(nullptr) { set(std::forward<decltype(args)>(args)...); }
public:
	block_view_lazy(const block_view_lazy<T, CacheType>& other) : block<T>(other), cache(other.cache), object(other.object) { if (object) { cache->inc_ref(*this); } }
	block_view_lazy(block_view_lazy<T, CacheType>&& other) : block<T>(std::move(other)), cache(other.cache), object(other.object) { other.object = nullptr; }
	~block_view_lazy() { drop(); }
private:
	CacheType* cache;
	mutable const T* object;
public:
	block_view_lazy& drop()& { if (object) { cache->dec_ref(*this); object = nullptr; } return *this; }
	block_view_lazy&& drop()&& { if (object) { cache->dec_ref(*this); object = nullptr; } return std::move(*this); }
public:
	block_view_lazy& operator=(const block<T>& other) { drop(); block<T>::operator=(other); return *this; }
	block_view_lazy& operator=(block_view_lazy<T, CacheType>&& other) { drop(); block<T>::operator=(std::move(other)); cache = other.cache; object = other.object; other.object = nullptr; return *this; }
	block_view_lazy& operator=(const block_view_lazy<T, CacheType>& other) { drop(); block<T>::operator=(other); cache = other.cache; object = other.object; if (object) { cache->inc_ref(*this); } return *this; }
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

template<class T, class CacheType>
class block_view : public block_view_lazy<T, CacheType> {
public:
	using block_view_lazy<T, CacheType>::block_view_lazy;
	block_view(const block_view_lazy<T, CacheType>& other) : block_view_lazy<T, CacheType>(other) { block_view_lazy<T, CacheType>::get(); }
	block_view(const block_view_lazy<T, CacheType>& other, auto init) : block_view_lazy<T, CacheType>(other) { block_view_lazy<T, CacheType>::get(std::forward<decltype(init)>(init)); }
	block_view(block_view_lazy<T, CacheType>&& other) : block_view_lazy<T, CacheType>(std::move(other)) { block_view_lazy<T, CacheType>::get(); }
	block_view(block_view_lazy<T, CacheType>&& other, auto init) : block_view_lazy<T, CacheType>(std::move(other)) { block_view_lazy<T, CacheType>::get(std::forward<decltype(init)>(init)); }
public:
	block_view& operator=(const block<T>& other) { block_view_lazy<T, CacheType>::operator=(other); block_view_lazy<T, CacheType>::get(); return *this; }
	block_view& operator=(block_view_lazy<T, CacheType>&& other) { block_view_lazy<T, CacheType>::operator=(std::move(other)); block_view_lazy<T, CacheType>::get(); return *this; }
	block_view& operator=(const block_view_lazy<T, CacheType>& other) { block_view_lazy<T, CacheType>::operator=(other); block_view_lazy<T, CacheType>::get(); return *this; }
};


template<class T>
class BlockCache {
public:
	BlockCache(BlockManager& manager) : manager(manager) {}

private:
	BlockManager& manager;

private:
	struct Entry {
		block_ref ref;
		size_t count;
		T object;
	};
private:
	std::unordered_map<ref_t, Entry> map;
private:
	bool has(ref_t ref) { return map.contains(ref); }
	T& get(ref_t ref) { auto& entry = map.at(ref); entry.count++; return entry.object; }
	T& set(const block_ref& ref, T object) { return map.emplace(ref, Entry{ ref, 1, std::move(object) }).first->second.object; }

private:
	friend class block_view_lazy<T, BlockCache>;
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
			static_cast<block<T>&>(entry.ref).write(entry.object);
		}
	}
	void end_commit() {
		dirty.clear();
	}

private:
	const T& lookup_read(const block<T>& ref) {
		if (has(ref)) {
			return get(ref);
		} else {
			return set(ref, ref.read());
		}
	}
	const T& lookup_read(const block<T>& ref, auto init) {
		if (has(ref)) {
			return get(ref);
		} else {
			return set(ref, ref.read(std::forward<decltype(init)>(init)));
		}
	}
	const T& lookup_write(block<T>& ref, auto&&... args) {
		return transaction([&] -> decltype(auto) {
			if (has(ref)) {
				auto& object = get(ref);
				object = T(std::forward<decltype(args)>(args)...);
				mark(ref);
				return object;
			} else {
				auto& object = set(ref, T(std::forward<decltype(args)>(args)...));
				mark(ref);
				return object;
			}
		});
	}
	const T& update(ref_t ref, const T& object, auto f) {
		return transaction([&] -> decltype(auto) {
			f(const_cast<T&>(object));
			mark(ref);
			return object;
		});
	}

public:
	block_view_lazy<T, BlockCache<T>> read_lazy(block<T> ref) {
		return block_view_lazy<T, BlockCache<T>>(std::move(ref), *this);
	}
	block_view<T, BlockCache<T>> read(block<T> ref) {
		return block_view<T, BlockCache<T>>(read_lazy(std::move(ref)));
	}
	block_view<T, BlockCache<T>> read(block<T> ref, auto init) {
		return block_view<T, BlockCache<T>>(read_lazy(std::move(ref)), std::forward<decltype(init)>(init));
	}
	block_view<T, BlockCache<T>> create(auto&&... args) {
		return block_view<T, BlockCache<T>>(manager.allocate(), *this, std::in_place, std::forward<decltype(args)>(args)...);
	}

private:
	size_t transaction_level = 0;
public:
	decltype(auto) transaction(auto f) {
		if constexpr (std::is_void_v<std::invoke_result_t<decltype(f)>>) {
			manager.transaction([&] {
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
			decltype(auto) res = manager.transaction([&] -> decltype(auto) {
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


class BlockCacheDynamic {
public:
	BlockCacheDynamic(BlockManager& manager) : manager(manager) {}

protected:
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
	template<class T, class CacheType> friend class block_view_lazy;
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

protected:
	template<class T>
	const T& lookup_read(const block<T>& ref) {
		if (has(ref)) {
			return get<T>(ref);
		} else {
			return set<T>(ref, ref.read());
		}
	}
	template<class T>
	const T& lookup_read(const block<T>& ref, auto init) {
		if (has(ref)) {
			return get<T>(ref);
		} else {
			return set<T>(ref, ref.read(std::forward<decltype(init)>(init)));
		}
	}
	template<class T>
	const T& lookup_write(block<T>& ref, auto&&... args) {
		return transaction([&] -> decltype(auto) {
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
	const T& update(ref_t ref, const T& object, auto f) {
		return transaction([&] -> decltype(auto) {
			f(const_cast<T&>(object));
			mark(ref);
			return object;
		});
	}

public:
	template<class T>
	block_view_lazy<T, BlockCacheDynamic> read_lazy(block<T> ref) {
		return block_view_lazy<T, BlockCacheDynamic>(std::move(ref), *this);
	}
	template<class T>
	block_view<T, BlockCacheDynamic> read(block<T> ref) {
		return block_view<T, BlockCacheDynamic>(read_lazy(std::move(ref)));
	}
	template<class T>
	block_view<T, BlockCacheDynamic> read(block<T> ref, auto init) {
		return block_view<T, BlockCacheDynamic>(read_lazy(std::move(ref)), std::forward<decltype(init)>(init));
	}
	template<class T>
	block_view<T, BlockCacheDynamic> create(auto&&... args) {
		return block_view<T, BlockCacheDynamic>(manager.allocate(), *this, std::in_place, std::forward<decltype(args)>(args)...);
	}

private:
	size_t transaction_level = 0;
public:
	decltype(auto) transaction(auto f) {
		if constexpr (std::is_void_v<std::invoke_result_t<decltype(f)>>) {
			manager.transaction([&] {
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
			decltype(auto) res = manager.transaction([&] -> decltype(auto) {
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

public:
	template<class Adapter>
	operator Adapter& () { return static_cast<Adapter&>(*this); }
};

template<class T>
class BlockCacheDynamicAdapter : public BlockCacheDynamic {
public:
	using BlockCacheDynamic::BlockCacheDynamic;
public:
	block_view_lazy<T, BlockCacheDynamicAdapter<T>> read_lazy(block<T> ref) {
		return block_view_lazy<T, BlockCacheDynamicAdapter<T>>(std::move(ref), *this);
	}
	block_view<T, BlockCacheDynamicAdapter<T>> read(block<T> ref) {
		return block_view<T, BlockCacheDynamicAdapter<T>>(read_lazy(std::move(ref)));
	}
	block_view<T, BlockCacheDynamicAdapter<T>> read(block<T> ref, auto init) {
		return block_view<T, BlockCacheDynamicAdapter<T>>(read_lazy(std::move(ref)), std::forward<decltype(init)>(init));
	}
	block_view<T, BlockCacheDynamicAdapter<T>> create(auto&&... args) {
		return block_view<T, BlockCacheDynamicAdapter<T>>(manager.allocate(), *this, std::in_place, std::forward<decltype(args)>(args)...);
	}
};


template<class T>
class BlockCacheLocal;


template<class T>
class block_view_lazy<T, BlockCacheLocal<T>> : public block<T> {
private:
	friend class BlockCacheLocal<T>;
private:
	block_view_lazy(block<T> ref) : block<T>(std::move(ref)), object(std::nullopt) {}
	block_view_lazy(block<T> ref, std::in_place_t, auto&&... args) : block<T>(std::move(ref)), object(std::nullopt) { set(std::forward<decltype(args)>(args)...); }
public:
	block_view_lazy(const block_view_lazy<T, BlockCacheLocal<T>>& other) : block<T>(other), object(other.object) {}
	block_view_lazy(block_view_lazy<T, BlockCacheLocal<T>>&& other) : block<T>(std::move(other)), object(std::move(other.object)) {}
public:
	block_view_lazy& drop()& { object.reset(); return *this; }
	block_view_lazy&& drop()&& { object.reset(); return std::move(*this); }
public:
	block_view_lazy& operator=(const block<T>& other) { drop(); block<T>::operator=(other); return *this; }
	block_view_lazy& operator=(block_view_lazy<T, BlockCacheLocal<T>>&& other) { drop(); block<T>::operator=(std::move(other)); object = std::move(other.object); return *this; }
	block_view_lazy& operator=(const block_view_lazy<T, BlockCacheLocal<T>>& other) { drop(); block<T>::operator=(other); object = other.object; return *this; }
private:
	block<T>::read;
	block<T>::write;
private:
	mutable std::optional<T> object;
public:
	const T& get() const { if (!object) { object.emplace(block<T>::read()); } return *object; }
	const T& get(auto init) const { if (!object) { object.emplace(block<T>::read(std::forward<decltype(init)>(init))); } return *object; }
	const T& set(auto&&... args) { object.emplace(std::forward<decltype(args)>(args)...); block<T>::write(*object); return *object; }
	const T& update(auto f) { return block_ref::get_manager().transaction([&] -> decltype(auto) { const T& val = get(); f(const_cast<T&>(*object)); block<T>::write(*object); return val; }); }
	const T& update(auto f, auto init) { return block_ref::get_manager().transaction([&] -> decltype(auto) { const T& val = get(std::forward<decltype(init)>(init)); f(const_cast<T&>(*object)); block<T>::write(*object); return val; }); }
};

template<class T>
class block_view<T, BlockCacheLocal<T>> : public block_view_lazy<T, BlockCacheLocal<T>> {
public:
	using block_view_lazy<T, BlockCacheLocal<T>>::block_view_lazy;
	block_view(const block_view_lazy<T, BlockCacheLocal<T>>& other) : block_view_lazy<T, BlockCacheLocal<T>>(other) { block_view_lazy<T, BlockCacheLocal<T>>::get(); }
	block_view(const block_view_lazy<T, BlockCacheLocal<T>>& other, auto init) : block_view_lazy<T, BlockCacheLocal<T>>(other) { block_view_lazy<T, BlockCacheLocal<T>>::get(std::forward<decltype(init)>(init)); }
	block_view(block_view_lazy<T, BlockCacheLocal<T>>&& other) : block_view_lazy<T, BlockCacheLocal<T>>(std::move(other)) { block_view_lazy<T, BlockCacheLocal<T>>::get(); }
	block_view(block_view_lazy<T, BlockCacheLocal<T>>&& other, auto init) : block_view_lazy<T, BlockCacheLocal<T>>(std::move(other)) { block_view_lazy<T, BlockCacheLocal<T>>::get(std::forward<decltype(init)>(init)); }
public:
	block_view& operator=(const block<T>& other) { block_view_lazy<T, BlockCacheLocal<T>>::operator=(other); block_view_lazy<T, BlockCacheLocal<T>>::get(); return *this; }
	block_view& operator=(block_view_lazy<T, BlockCacheLocal<T>>&& other) { block_view_lazy<T, BlockCacheLocal<T>>::operator=(std::move(other)); block_view_lazy<T, BlockCacheLocal<T>>::get(); return *this; }
	block_view& operator=(const block_view_lazy<T, BlockCacheLocal<T>>& other) { block_view_lazy<T, BlockCacheLocal<T>>::operator=(other); block_view_lazy<T, BlockCacheLocal<T>>::get(); return *this; }
};


template<class T>
using block_view_local_lazy = block_view_lazy<T, BlockCacheLocal<T>>;

template<class T>
using block_view_local = block_view<T, BlockCacheLocal<T>>;


template<class T>
class BlockCacheLocal {
public:
	BlockCacheLocal(BlockManager& manager) : manager(manager) {}

private:
	BlockManager& manager;

public:
	static void sweep() {}

public:
	static block_view_local_lazy<T> read_lazy(block<T> ref) {
		return block_view_local_lazy<T>(std::move(ref));
	}
	static block_view_local<T> read(block<T> ref) {
		return block_view_local<T>(read_lazy(std::move(ref)));
	}
	static block_view_local<T> read(block<T> ref, auto init) {
		return block_view_local<T>(read_lazy(std::move(ref)), std::forward<decltype(init)>(init));
	}
	block_view_local<T> create(auto&&... args) {
		return block_view_local<T>(manager.allocate(), std::in_place, std::forward<decltype(args)>(args)...);
	}

public:
	decltype(auto) transaction(auto f) { return manager.transaction(std::forward<decltype(f)>(f)); }
};


} // namespace BlockStore
