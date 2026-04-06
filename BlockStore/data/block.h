#pragma once

#include "serializer.h"


namespace BlockStore {

constexpr size_t block_size_limit = 4096; // byte


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
			throw std::invalid_argument("block data uninitialized");
		} else {
			return DeserializeContext(get_manager(), std::move(data)).access<T>();
		}
	}
	T read(auto init) const {
		if (auto data = block_ref::read(); data.empty()) {
			T object(init());
			auto [size, ref_size] = SizeContext().access(object).Get();
			if (ref_size > 0) {
				const_cast<block<T>&>(*this).write(object);
			}
			return object;
		} else {
			return DeserializeContext(get_manager(), std::move(data)).access<T>();
		}
	}
	void write(const T& object) {
		auto [data, ref_list] = SerializeContext(get_manager()).access(object).Get();
		if (data.size() > block_size_limit) {
			throw std::invalid_argument("block size exceeds limit");
		}
		block_ref::write(data, ref_list);
	}
};


using CppSerialize::layout_type;
using CppSerialize::layout;
using CppSerialize::declare;

} // namespace BlockStore
