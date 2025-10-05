#pragma once

#include <iostream>


template <typename T>
struct reversion_wrapper { T& iterable; };

template <typename T>
auto begin(reversion_wrapper<T> w) { return std::rbegin(w.iterable); }

template <typename T>
auto end(reversion_wrapper<T> w) { return std::rend(w.iterable); }

template <typename T>
reversion_wrapper<T> reverse(T& iterable) { return { iterable }; }


inline void print(const auto& container) {
	for (auto i : container) {
		std::cout << i << ' ';
	}
	std::cout << std::endl;

	for (auto i : reverse(container)) {
		std::cout << i << ' ';
	}
	std::cout << std::endl;
	std::cout << std::endl;
}
