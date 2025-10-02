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
public:
	using layout_base = block_ref;
};


using CppSerialize::layout_type;
using CppSerialize::layout;
using CppSerialize::declare;

END_NAMESPACE(BlockStore)