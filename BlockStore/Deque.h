#include "List.h"
#include "CppSerialize/stl/vector.h"

#include <cassert>


BEGIN_NAMESPACE(BlockStore)

constexpr size_t deque_block_size_limit = block_size_limit - 2 * sizeof(index_t) - sizeof(size_t);

template<class T>
constexpr size_t deque_block_limit = deque_block_size_limit / layout_traits<T>::size();


template<class T>
class Deque {
public:
	static constexpr size_t block_limit = deque_block_limit<T>;

private:
	struct Node {
		block<Node> next;
		block<Node> prev;
		std::vector<T> data;

		Node() = default;
		Node(const block_ref& root) : next(root), prev(root), data() {}
		Node(const block<Node>& next, const block<Node>& prev, std::vector<T> data) : next(next), prev(prev), data(std::move(data)) {}

		friend constexpr auto layout(layout_type<Node>) { return declare(&Node::next, &Node::prev, &Node::data); }
	};

public:
	class iterator {
	private:
		friend class Deque;

	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T*;
		using reference = T&;

	private:
		const Deque* deque;
		block_cache_lazy<Node> curr;
		size_t curr_index;

	private:
		iterator(const Deque& deque, block_cache_lazy<Node> curr, size_t curr_index) : deque(&deque), curr(curr), curr_index(curr_index) {}

	public:
		bool operator==(const iterator& other) const { return curr == other.curr && curr_index == other.curr_index; }

		const T& operator*() {
			if (curr_index == curr.get().data.size()) {
				assert(curr == deque->tail);
				throw std::invalid_argument("cannot dereference end list iterator");
			}
			return curr.get().data[curr_index];
		}

		iterator& operator++() {
			if (curr_index == curr.get().data.size()) {
				assert(curr == deque->tail);
				throw std::invalid_argument("cannot increment end list iterator");
			}
			curr_index++;
			if (curr_index == curr.get().data.size() && curr != deque->tail) {
				curr = curr.get().next;
				curr_index = 0;
			}
			return *this;
		}

		iterator& operator+=(size_t offset) {
			for (curr_index += offset;;) {
				const size_t curr_size = curr.get().data.size();
				if (curr_index >= curr_size) {
					if (curr == deque->tail) {
						throw std::invalid_argument("cannot increment end list iterator");
					}
					curr = curr.get().next;
					curr_index -= curr_size;
				} else {
					break;
				}
			};
			return *this;
		}

		iterator& operator--() {
			if (curr_index == 0) {
				if (curr == deque->head) {
					throw std::invalid_argument("cannot decrement begin list iterator");
				}
				curr = curr.get().prev;
				curr_index = curr.get().data.size();
				assert(curr_index > 0);
			}
			curr_index--;
			return *this;
		}

		iterator& operator-=(size_t offset) {
			for (;;) {
				if (curr_index < offset) {
					if (curr == deque->head) {
						throw std::invalid_argument("cannot increment end list iterator");
					}
					offset -= curr_index;
					curr = curr.get().prev;
					curr_index = curr.get().data.size();
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
	Deque(block<Node> root) : head(root, [&]() { return Node(root); }), tail(head.get().prev) {}

private:
	block_cache<Node> head;
	block_cache<Node> tail;

public:
	bool empty() const { return head.get().data.empty(); }

	iterator begin() const { return iterator(*this, head, 0); }
	iterator end() const { return iterator(*this, tail, tail.get().data.size()); }

	std::reverse_iterator<iterator> rbegin() const { return std::reverse_iterator<iterator>(end()); }
	std::reverse_iterator<iterator> rend() const { return std::reverse_iterator<iterator>(begin()); }

	const T& front() const {
		if (empty()) {
			throw std::invalid_argument("deque is empty");
		}
		return *begin();
	}

	const T& back() const {
		if (empty()) {
			throw std::invalid_argument("deque is empty");
		}
		return *--end();
	}

public:
	void clear() {
		if (empty()) {
			return;
		}
		head.set(Node(head));
		tail = head;
	}

	iterator emplace_back(auto&&... args) {
		return emplace(end(), std::forward<decltype(args)>(args)...);
	}

	iterator emplace_front(auto&&... args) {
		return emplace(begin(), std::forward<decltype(args)>(args)...);
	}

	iterator emplace(iterator pos, auto&&... args) {
		block_cache<Node> curr = pos.curr;
		if (curr.get().data.size() < block_limit) {
			curr.update([&](Node& n) { n.data.emplace(n.data.begin() + pos.curr_index, std::forward<decltype(args)>(args)...); });
		} else {
			if (pos.curr_index == curr.get().data.size()) {
				assert(pos == end());
				tail = block_manager.transaction([&]() {
					block_cache<Node> new_node(std::in_place, head, tail, [&] { std::vector<T> data; data.emplace_back(std::forward<decltype(args)>(args)...); return data; }());
					if (head == tail) {
						head.update([&](Node& n) { n.next = n.prev = new_node; });
					} else {
						head.update([&](Node& n) { n.prev = new_node; });
						tail.update([&](Node& n) { n.next = new_node; });
					}
					pos = iterator(*this, new_node, 0);
					return new_node;
				});
			} else {
				tail = block_manager.transaction([&]() {
					block_cache<Node> next = curr.get().next;
					block_cache_lazy<Node> new_node;
					if (curr != next) {
						next.update([&](Node& n) { n.prev = new_node; });
					}
					new_node.set(next, curr, [&] {
						std::vector<T> data;
						curr.update([&](Node& n) {
							n.next = new_node;
							if (curr == next) {
								n.prev = new_node;
							}
							if (pos.curr_index <= block_limit / 2) {
								if (pos.curr_index == 0) {
									data = std::move(n.data);
								} else {
									data.insert(data.end(), std::make_move_iterator(n.data.begin() + pos.curr_index), std::make_move_iterator(n.data.end()));
									n.data.erase(n.data.begin() + pos.curr_index, n.data.end());
								}
								n.data.emplace_back(std::forward<decltype(args)>(args)...);
							} else {
								data.emplace_back(std::forward<decltype(args)>(args)...);
								data.insert(data.end(), std::make_move_iterator(n.data.begin() + pos.curr_index), std::make_move_iterator(n.data.end()));
								n.data.erase(n.data.begin() + pos.curr_index, n.data.end());
								pos = iterator(*this, new_node, 0);
							}
						});
						return data;
					}());
					return next == head ? static_cast<block_cache<Node>>(new_node) : tail;
				});
			}
		}
		return pos;
	}

	iterator pop_back() {
		if (empty()) {
			throw std::invalid_argument("deque is empty");
		}
		return erase(--end());
	}

	iterator pop_front() {
		if (empty()) {
			throw std::invalid_argument("deque is empty");
		}
		return erase(begin());
	}

	iterator erase(iterator pos) {
		if (pos.curr_index >= pos.curr.get().data.size()) {
			assert(head == tail);
			assert(pos.curr == head);
			throw std::invalid_argument("deque erase iterator outside range");
		}
		if (pos.curr.get().data.size() > 1 || head == tail) {
			pos.curr.update([&](Node& n) { n.data.erase(n.data.begin() + pos.curr_index); });
			if (pos.curr_index < pos.curr.get().data.size() || pos.curr == tail) {
				return pos;
			} else {
				return iterator(*this, pos.curr.get().next, 0);
			}
		} else {
			if (pos.curr == tail) {
				tail = block_manager.transaction([&]() {
					block_cache<Node> prev(tail.get().prev);
					if (prev == head) {
						head.update([&](Node& n) { n.next = n.prev = head; });
					} else {
						prev.update([&](Node& n) { n.next = head; });
						head.update([&](Node& n) { n.prev = prev; });
					}
					return prev;
				});
				return end();
			} else if (pos.curr == head) {
				tail = block_manager.transaction([&]() {
					block_cache<Node> next(head.get().next);
					block_cache<Node> nnext(next.get().next);
					if (head == nnext) {
						head.update([&](Node& n) { n.next = n.prev = head; next.update([&](Node& nn) { std::swap(n.data, nn.data); }); });
						return head;
					} else {
						head.update([&](Node& n) { n.next = nnext; next.update([&](Node& nn) { std::swap(n.data, nn.data); }); });
						nnext.update([&](Node& n) { n.prev = head; });
						return tail;
					}
				});
				return begin();
			} else {
				return block_manager.transaction([&]() {
					block_cache<Node> prev(pos.curr.get().prev);
					block_cache<Node> next(pos.curr.get().next);
					assert(prev != next);
					prev.update([&](Node& n) { n.next = next; });
					next.update([&](Node& n) { n.prev = prev; });
					return iterator(*this, next, 0);
				});
			}
		}
	}
};


template<class T> requires (deque_block_limit<T> <= 1)
class Deque<T> : public List<T> {
public:
	using List<T>::List;
};


END_NAMESPACE(BlockStore)