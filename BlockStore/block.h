#pragma once

#include "serializer.h"

#include "CppSerialize/layout_traits_stl.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
class block : public block_ref {
public:
	block() : block_ref() {}
	block(block_ref ref) : block_ref(ref) {}
public:
	T read() {
		if (auto data = block_ref::read(); !data.empty()) {
			deserialize_begin();
			T object;
			BlockLoadContext load_context(data.data(), data.size());
			load_context.load(object);
			deserialize_end();
			return object;
		} else {
			T object;
			BlockSizeContext size_context; size_context.add(object);
			if (size_context.GetIndexSize()) {
				write(object);
			}
			return object;
		}
	}
	void write(const T& object) {
		BlockSizeContext size_context; size_context.add(object);
		std::vector<byte> data(size_context.GetSize()); std::vector<index_t> ref(size_context.GetIndexSize());
		BlockSaveContext save_context(data.data(), data.size(), ref.data(), ref.size());
		save_context.save(object);
		block_ref::write(std::move(data), std::move(ref));
	}
};


END_NAMESPACE(BlockStore)


BEGIN_NAMESPACE(CppSerialize)


template<class T>
constexpr bool has_trivial_layout<BlockStore::block<T>> = true;


END_NAMESPACE(CppSerialize)