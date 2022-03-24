#pragma once

#include "block_data.h"
#include "layout_traits.h"


BEGIN_NAMESPACE(BlockStore)


class block_ref {
private:
	friend struct BlockManager;
	friend struct layout_traits<block_ref>;

public:
	block_ref() : index(block_index_invalid) {}
private:
	block_ref(data_t block_index) : index(block_index) {}

private:
	data_t index;
public:
	bool empty() const { return index == block_index_invalid; }
	void clear() { index = block_index_invalid; }

private:
	void CreateBlock();
	void SetBlockData(block_data block_data);
	block_data GetBlockData();

public:
	template<class T> void read(T& object) {
		if (empty()) { return; }
		block_data data = GetBlockData();
		BlockLoadContext context(data.first.data(), data.first.size(), data.second.data(), data.second.size());
		Load(context, object);
	}
	template<class T> T read() {
		T object; read(object); return object;
	}
	template<class T> void write(const T& object) {
		if (empty()) { CreateBlock(); }
		BlockSizeContext size_context; Size(size_context, object);
		std::vector<byte> data(size_context.GetSize()); std::vector<data_t> index_data(size_context.GetIndexSize());
		BlockSaveContext context(data.data(), data.size(), index_data.data(), index_data.size()); Save(context, object);
		SetBlockData({ std::move(data), std::move(index_data) });
	}
};


template<>
struct layout_traits<block_ref> {
	static void Size(BlockSizeContext& context, const block_ref& object) { context.add_index(); }
	static void Save(BlockSaveContext& context, const block_ref& object) { context.write_index(object.index); }
	static void Load(BlockLoadContext& context, block_ref& object) { context.read_index(object.index); }
};


END_NAMESPACE(BlockStore)