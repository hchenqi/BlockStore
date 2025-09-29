#pragma once

#include "serializer.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
class block : public block_ref {
public:
	block() : block_ref() {}
	block(block_ref ref) : block_ref(ref) {}
public:
	T read(auto init) const {
		if (auto data = block_ref::read(); !data.empty()) {
			return BlockDeserialize<T>(data).Get();
		} else {
			T object(init());
			auto [size, ref_size] = BlockSize(object).Get();
			if (ref_size > 0) {
				const_cast<block<T>&>(*this).write(object);
			}
			return object;
		}
	}
	T read() const {
		return read([]() { return T(); });
	}
	void write(const T& object) {
		auto [data, ref] = BlockSerialize(object).Get();
		block_ref::write(data, ref);
	}
};


END_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(CppSerialize)


template<class T>
struct layout_traits<BlockStore::block<T>> {
	constexpr static layout_size size() { return layout_size(layout_type<BlockStore::block_ref>()); }
	constexpr static void read(auto f, const auto& object) { return f(static_cast<const BlockStore::block_ref&>(object)); }
	constexpr static void write(auto f, auto& object) { return f(static_cast<BlockStore::block_ref&>(object)); }
};


END_NAMESPACE(CppSerialize)