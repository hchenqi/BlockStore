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
	std::vector<byte> read();
	void write(std::vector<byte> data, std::vector<index_t> ref);
};

template<class T>
class block : public block<> {
public:
	T read() {
		T object;
		if (!empty()) {
			auto data = block<>::read();
			BlockLoadContext load_context(data.data(), data.size());
			load_context.load(object);
		}
		return object;
	}
	void write(const T& object) {
		BlockSizeContext size_context; size_context.add(object);
		std::vector<byte> data(size_context.GetSize()); std::vector<index_t> ref(size_context.GetIndexSize());
		BlockSaveContext save_context(data.data(), data.size(), ref.data(), ref.size());
		save_context.save(object);
		block<>::write(std::move(data), std::move(ref));
	}
};


END_NAMESPACE(BlockStore)