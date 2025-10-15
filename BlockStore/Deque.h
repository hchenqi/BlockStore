#include "List.h"
#include "CppSerialize/stl/vector.h"

#include <cassert>


BEGIN_NAMESPACE(BlockStore)

constexpr size_t deque_block_size_limit = block_size_limit - 2 * sizeof(index_t) - sizeof(size_t);

template<class T>
constexpr size_t deque_block_limit = deque_block_size_limit / layout_traits<T>::size();


template<class T>
class Deque : private List<std::vector<T>> {
private:
	using List = List<std::vector<T>>;

public:
	static constexpr size_t block_limit = deque_block_limit<T>;

public:
	class value_wrapper : private List::value_wrapper {
	private:
		friend class Deque;

	private:
		size_t index;

	private:
		value_wrapper(const List::value_wrapper& list_value_wrapper, size_t index) : List::value_wrapper(list_value_wrapper), index(index) {}

	private:
		List::value_wrapper& list_value_wrapper() { return *this; }
		const List::value_wrapper& list_value_wrapper() const { return *this; }

	public:
		const T& get() const { return list_value_wrapper().get().at(index); }
		const T& set(auto&&... args) { return update([&](T& object) { object = T(std::forward<decltype(args)>(args)...); }); }
		const T& update(auto f) { return list_value_wrapper().update([&](auto& vector) { f(vector.at(index)); }).at(index); }

		operator const T& () const { return get(); }
		const T* operator->() const { return &get(); }
	};

	class iterator : private List::iterator {
	private:
		friend class Deque;

	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T*;
		using reference = T&;

	private:
		size_t curr_index;

	private:
		iterator(const List::iterator& list_iterator, size_t curr_index) : List::iterator(list_iterator), curr_index(curr_index) {}

	private:
		List::iterator& list_iterator() { return *this; }
		const List::iterator& list_iterator() const { return *this; }

	public:
		bool operator==(const iterator& other) const { return list_iterator() == other && curr_index == other.curr_index; }

		value_wrapper operator*() {
			return value_wrapper(*list_iterator(), curr_index);
		}

		const T* operator->() {
			return (**this).operator->();
		}

		iterator& operator++() {
			assert(curr_index < list_iterator()->size());
			curr_index++;
			if (curr_index == list_iterator()->size()) {
				++list_iterator();
				curr_index = 0;
			}
			return *this;
		}

		iterator& operator+=(size_t offset) {
			for (curr_index += offset;;) {
				const size_t curr_size = list_iterator()->size();
				if (curr_index >= curr_size) {
					++list_iterator();
					curr_index -= curr_size;
				} else {
					break;
				}
			};
			return *this;
		}

		iterator& operator--() {
			if (curr_index == 0) {
				--list_iterator();
				curr_index = list_iterator()->size();
				assert(curr_index > 0);
			}
			curr_index--;
			return *this;
		}

		iterator& operator-=(size_t offset) {
			for (;;) {
				if (curr_index < offset) {
					offset -= curr_index;
					--list_iterator();
					curr_index = list_iterator()->size();
					assert(curr_index > 0);
				} else {
					curr_index -= offset;
					break;
				}
			};
			return *this;
		}
	};

public:
	Deque(const block_ref& root) : List(root) {}

public:
	using List::empty;

	iterator begin() const { return iterator(List::begin(), 0); }
	iterator end() const { return iterator(List::end(), 0); }

	std::reverse_iterator<iterator> rbegin() const { return std::reverse_iterator<iterator>(end()); }
	std::reverse_iterator<iterator> rend() const { return std::reverse_iterator<iterator>(begin()); }

	value_wrapper front() const { return value_wrapper(List::front(), 0); }
	value_wrapper back() const { auto list_back = List::back(); return value_wrapper(list_back, list_back.get().size() - 1); }

public:
	using List::clear;

	iterator emplace_back(auto&&... args) {
		return block_manager.transaction([&] {
			auto it = empty() ? List::emplace_back() : --List::end();
			if (it->size() >= block_limit) {
				it = List::emplace_back();
			}
			(*it).update([&](auto& vector) { vector.emplace_back(std::forward<decltype(args)>(args)...); });
			return iterator(it, it->size() - 1);
		});
	}

	iterator emplace_front(auto&&... args) {
		return block_manager.transaction([&] {
			auto it = empty() ? List::emplace_front() : List::begin();
			if (it->size() >= block_limit) {
				it = List::emplace_front();
			}
			(*it).update([&](auto& vector) { vector.emplace(vector.begin(), std::forward<decltype(args)>(args)...); });
			return iterator(it, 0);
		});
	}

	iterator emplace(iterator pos, auto&&... args) {
		if (pos == end()) {
			return emplace_back(std::forward<decltype(args)>(args)...);
		}
		return block_manager.transaction([&] {
			if (pos.list_iterator()->size() < block_limit) {
				(*pos.list_iterator()).update([&](auto& vector) { vector.emplace(vector.begin() + pos.curr_index, std::forward<decltype(args)>(args)...); });
				return pos;
			} else {
				if (pos.curr_index <= block_limit / 2) {
					auto prev = List::emplace(pos);
					if (pos.curr_index == 0) {
						(*prev).update([&](auto& prev) { prev.emplace_back(std::forward<decltype(args)>(args)...); });
					} else {
						(*prev).update([&](auto& prev) { (*pos.list_iterator()).update([&](auto& curr) {
							prev.insert(prev.end(), std::make_move_iterator(curr.begin()), std::make_move_iterator(curr.begin() + pos.curr_index));
							prev.emplace_back(std::forward<decltype(args)>(args)...);
							curr.erase(curr.begin(), curr.begin() + pos.curr_index);
						}); });
					}
					return iterator(prev, pos.curr_index);
				} else {
					auto next = List::emplace(std::next(pos));
					if (pos.curr_index == pos.list_iterator()->size()) {
						(*next).update([&](auto& next) { next.emplace_back(std::forward<decltype(args)>(args)...); });
					} else {
						(*next).update([&](auto& next) { (*pos.list_iterator()).update([&](auto& curr) {
							next.emplace_back(std::forward<decltype(args)>(args)...);
							next.insert(next.end(), std::make_move_iterator(curr.begin() + pos.curr_index), std::make_move_iterator(curr.end()));
							curr.erase(curr.begin() + pos.curr_index, curr.end());
						}); });
					}
					return iterator(next, 0);
				}
			}
		});
	}

	iterator pop_back() {
		if (empty()) {
			throw std::invalid_argument("deque is empty");
		}
		return block_manager.transaction([&] {
			if (auto back = List::back(); back->size() > 1) {
				back.update([](auto& vector) { vector.pop_back(); });
			} else {
				List::pop_back();
			}
			return end();
		});
	}

	iterator pop_front() {
		if (empty()) {
			throw std::invalid_argument("deque is empty");
		}
		return block_manager.transaction([&] {
			if (auto front = List::front(); front->size() > 1) {
				front.update([](auto& vector) { vector.erase(vector.begin()); });
			} else {
				List::pop_front();
			}
			return begin();
		});
	}

	iterator erase(iterator pos) {
		if (pos == end()) {
			throw std::invalid_argument("deque erase iterator outside range");
		}
		return block_manager.transaction([&] {
			if (pos.list_iterator()->size() > 1) {
				(*pos.list_iterator()).update([&](auto& vector) { vector.erase(vector.begin() + pos.curr_index); });
				if (pos.curr_index < pos.list_iterator()->size()) {
					return pos;
				} else {
					return iterator(std::next(pos.list_iterator()), 0);
				}
			} else {
				return iterator(List::erase(pos), 0);
			}
		});
	}
};


template<class T> requires (deque_block_limit<T> <= 1)
class Deque<T> : public List<T> {
public:
	using List<T>::List;
};


END_NAMESPACE(BlockStore)