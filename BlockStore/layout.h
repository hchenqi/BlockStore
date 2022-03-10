#pragma once

#include "core.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
struct layout_type {};

template<class T>
constexpr auto layout(layout_type<T>) {}

template<class T, class... Ts>
constexpr auto declare(Ts T::*... member_list) { return std::make_tuple(member_list...); }


template<class T, class... Ts>
constexpr auto member_type_tuple(std::tuple<Ts T::*...>) -> std::tuple<Ts...> { return {}; }

template<class T, class = void>
constexpr bool has_custom_layout = false;

template<class T>
constexpr bool has_custom_layout<T, decltype(member_type_tuple(layout(layout_type<T>())), void())> = true;

template<class T>
constexpr bool has_trivial_layout = std::is_trivial_v<T> && !has_custom_layout<T>;


END_NAMESPACE(BlockStore)