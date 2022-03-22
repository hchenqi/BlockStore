#pragma once

#include "block_data.h"
#include "layout_traits.h"

#include <memory>


BEGIN_NAMESPACE(BlockStore)


template<class T = void>
class block_ref;


template<>
class block_ref<> {
private:
	friend struct BlockManager;

private:
	block_ref(data_t block_index) : stored(block_index != block_index_invalid), index(block_index) {}
public:
	block_ref() : stored(false), index(block_index_invalid) {}

private:
	bool stored;
	data_t index;
public:
	bool empty() const { return index == block_index_invalid; }
	void clear() { *this = block_ref(); }

private:
	bool IsNewBlock() const { return stored == false; }
private:
	bool IsNewBlockCached() const { return !empty(); }
	void CacheNewBlock(std::shared_ptr<void> block);
	const std::shared_ptr<void>& GetCachedNewBlock();
	bool IsNewBlockSaved() const;
	data_t GetFileIndex() const;

private:
	void CreateBlock();
	void SetBlockData(data_t block_index, block_data block_data);
	block_data GetBlockData(data_t block_index);
private:
	bool IsBlockCached() const;
	void CacheBlock(std::shared_ptr<void> block);
	const std::shared_ptr<void>& GetCachedBlock();
	bool IsCachedBlockDirty();
	void SetCachedBlockDirty();
	void ResetCachedBlockDirty();

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
	template<class T> const T& read() const {
		return get<T>();
	}
	template<class T> T& write() {
		T& object = get<T>();
		if (!IsNewBlock()) { SetCachedBlockDirty(); }
		return object;
	}
public:
	template<class T> void save() {
		if (empty()) { return; }
		if (IsNewBlock()) { CreateBlock(); }
		if (IsCachedBlockDirty()) {



		}
	}
};


template<class T>
class block_ref : public block_ref<> {
public:
	block_ref() {}
	block_ref(block_ref<> ref) : block_ref<>(ref) {}
public:
	const T& read() const { return read<T>(); }
	T& write() { return write<T>(); }
public:
	void save() { save<T>(); }
};


END_NAMESPACE(BlockStore)