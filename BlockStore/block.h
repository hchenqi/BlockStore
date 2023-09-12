#pragma once

#include "serializer.h"

#include "CppSerialize/layout_traits_stl.h"


BEGIN_NAMESPACE(BlockStore)


template<class T = void>
class block;

template<>
class block<void> {
private:
	index_t index;

public:
	block() : index(block_index_invalid) {}
	block(block_ref ref) : index(ref.index) {}

public:
	bool empty() const { return index == block_index_invalid; }
	operator block_ref() const { return index; }

protected:
	using data = std::pair<std::vector<byte>, std::vector<index_t>>;
protected:
	data read();
	void write(data data);
};

template<class T>
class block : public block<> {
public:
	T read() {
		if (empty()) { return {}; }
		auto data = block<>::read();
		BlockLoadContext load_context(data.first.data(), data.first.size(), data.second.data(), data.second.size());
		T object; load_context.load(object);
		return object;
	}
	void write(const T& object) {
		BlockSizeContext size_context; size_context.add(object);
		std::vector<byte> data(size_context.GetSize()); std::vector<index_t> ref_list(size_context.GetIndexSize());
		BlockSaveContext save_context(data.data(), data.size(), ref_list.data(), ref_list.size());
		save_context.save(object);
		block<>::write(std::make_pair(std::move(data), std::move(ref_list)));
	}
};


END_NAMESPACE(BlockStore)