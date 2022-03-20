#pragma once

#include "layout.h"

#include <vector>


BEGIN_NAMESPACE(BlockStore)


template<class T> requires (has_trivial_layout<T>)
inline std::vector<byte> Serialize(const T& object) {
	std::vector<byte> data(sizeof(T));
	memcpy(data.data(), &object, sizeof(T));
	return data;
}


template<class T> requires (has_trivial_layout<T>)
inline T Deserialize(const std::vector<byte>& data) {
	if (data.size() != sizeof(T)) { throw std::runtime_error("data size mismatch"); }
	T object;
	memcpy(&object, data.data(), sizeof(T));
	return object;
}


END_NAMESPACE(BlockStore)