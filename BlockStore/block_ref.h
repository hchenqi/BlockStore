#pragma once

#include "block_data.h"
#include "layout_traits.h"

#include <memory>


BEGIN_NAMESPACE(BlockStore)

class BlockManager;


class block_ref {
public:
	block_ref() : manager(nullptr), index(block_index_invalid) {}
	block_ref(BlockManager& manager, data_t block_index) : manager(&manager), index(block_index) {}

private:
	ref_ptr<BlockManager> manager;
	data_t index;
private:
	bool IsNewBlock() const { return manager == nullptr; }
private:
	bool IsNewBlockCached() const { return index != block_index_invalid; }
	void CacheNewBlock(std::shared_ptr<void> block);
	const std::shared_ptr<void>& GetCachedNewBlock();
private:
	block_data GetBlockData(data_t block_index);
private:
	bool IsBlockCached() const;
	void CacheBlock(std::shared_ptr<void> block);
	const std::shared_ptr<void>& GetCachedBlock();
	void SetCachedBlockDirty();

private:
	template<class T>
	struct deleter {
		void operator()(T* object) { delete object; }
	};
	template<class T>
	static std::shared_ptr<T> pointer_cast(const std::shared_ptr<void>& ptr) {
		if (std::get_deleter<deleter<T>>(ptr) == nullptr) { throw std::runtime_error("pointer type mismatch"); }
		return std::static_pointer_cast<T>(ptr);
	}

private:
	template<class T> T& get() {
		if (IsNewBlock()) {
			if (IsNewBlockCached()) {
				return *pointer_cast<T>(GetCachedNewBlock());
			} else {
				std::shared_ptr<T> object(new T(), deleter<T>()); T& ref = *object;
				CacheNewBlock(std::move(object));
				return ref;
			}
		} else {
			if (IsBlockCached()) {
				return *pointer_cast<T>(GetCachedBlock());
			} else {
				std::shared_ptr<T> object(new T(), deleter<T>()); T& ref = *object;
				block_data data = GetBlockData();
				BlockLoadContext context(data.first.data(), data.first.size(), data.second.data(), data.second.size());
				Load(context, ref);
				CacheBlock(std::move(object));
				return ref;
			}
		}
	}
public:
	template<class T> const T& read() {
		return get<T>();
	}
	template<class T> T& write() {
		T& object = get<T>();
		if (!IsNewBlock()) { SetCachedBlockDirty(); }
		return object;
	}
};


template<class T>
class BlockRef : public block_ref {
public:
	const T& Read() { return read<T>(); }
	T& Write() { return write<T>(); }
};


END_NAMESPACE(BlockStore)