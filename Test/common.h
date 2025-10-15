#pragma once

#include <iostream>


template <class T>
concept reversible = requires(T c) { std::rbegin(c); std::rend(c); };


template <reversible T>
struct reversion_wrapper { T& iterable; };

template <reversible T>
auto begin(reversion_wrapper<T> w) { return std::rbegin(w.iterable); }

template <reversible T>
auto end(reversion_wrapper<T> w) { return std::rend(w.iterable); }

template <reversible T>
reversion_wrapper<T> reverse(T& iterable) { return { iterable }; }


inline void print(const auto& container) {
	for (auto i : container) {
		std::cout << i.get() << ' ';
	}
	std::cout << std::endl;

	if constexpr (reversible<decltype(container)>) {
		for (auto i : reverse(container)) {
			std::cout << i.get() << ' ';
		}
		std::cout << std::endl;
		std::cout << std::endl;
	}
}
