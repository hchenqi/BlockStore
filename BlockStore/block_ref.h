#pragma once

#include "block_data.h"
#include "layout_traits.h"

#include <memory>


BEGIN_NAMESPACE(BlockStore)

class BlockManager;


class block_ref {
private:
	friend class BlockManager;
private:
	block_ref(BlockManager& manager, data_t block_index) : manager(&manager), index(block_index) { if (empty()) { this->manager = nullptr; } }
public:
	block_ref() : manager(nullptr), index(block_index_invalid) {}
	block_ref(block_ref&& ref) noexcept : manager(ref.manager), index(ref.index) { ref.manager = nullptr; ref.index = block_index_invalid; }
	block_ref(const block_ref& ref) : manager(ref.manager), index(ref.index) {}
	~block_ref() { if () {} else {} }
public:
	void swap(block_ref& other) noexcept { std::swap(manager, other.manager); std::swap(index, other.index); }
	block_ref& operator=(block_ref&& ref) noexcept { block_ref tmp(std::move(ref)); swap(tmp); return *this; }
	block_ref& operator=(const block_ref& ref) { block_ref tmp(ref); swap(tmp); return *this; }

private:
	ref_ptr<BlockManager> manager;
	data_t index;
public:
	bool empty() const { return index == block_index_invalid; }
private:
	bool IsNewBlock() const { return manager == nullptr; }
private:
	bool IsNewBlockCached() const { return !empty(); }
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
			if (IsBlockCreated()) {

			}
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
	template<class T> const T& read() const {
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
	BlockRef() {}
	BlockRef(block_ref&& ref) : block_ref(std::move(ref)) {}
public:
	const T& read() const { return read<T>(); }
	T& write() { return write<T>(); }
};


END_NAMESPACE(BlockStore)