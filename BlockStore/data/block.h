#pragma once

#include "serializer.h"


namespace BlockStore {


template<class T>
class block : public block_ref {
public:
	using layout_base = block_ref;
public:
	using block_ref::block_ref;
	block(block_ref&& other) : block_ref(std::move(other)) {}
	block(const block_ref& other) : block_ref(other) {}
public:
	block& operator=(block_ref&& other) { block_ref::operator=(std::move(other)); return *this; }
	block& operator=(const block_ref& other) { block_ref::operator=(other); return *this; }
public:
	T read() const {
		if (auto data = block_ref::read(); data.empty()) {
			throw std::invalid_argument("block data empty");
		} else {
			return BlockDeserialize<T>(get_manager(), data).Get();
		}
	}
	T read(auto init) const {
		if (auto data = block_ref::read(); data.empty()) {
			T object(init());
			auto [size, ref_size] = BlockSize(object).Get();
			if (ref_size > 0) {
				const_cast<block<T>&>(*this).write(object);
			}
			return object;
		} else {
			return BlockDeserialize<T>(get_manager(), data).Get();
		}
	}
	void write(const T& object) {
		auto [data, ref] = BlockSerialize(get_manager(), object).Get();
		block_ref::write(data, ref);
	}
};


using CppSerialize::layout_type;
using CppSerialize::layout;
using CppSerialize::declare;

} // namespace BlockStore
