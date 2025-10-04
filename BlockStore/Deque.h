#include "List.h"
#include "CppSerialize/stl/vector.h"

#include <deque>


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
		Node(block_ref initial) : next(initial), prev(initial), data() {}
		Node(block<Node> next, block<Node> prev, std::vector<T> data) : next(next), prev(prev), data(std::move(data)) {}
	};
	friend constexpr auto layout(layout_type<Node>) { return declare(&Node::next, &Node::prev, &Node::data); }

public:
	class iterator {
	private:
		friend class Deque;

	private:
		const Deque& deque;
		block_cache<Node> curr;
		size_t curr_index;

	public:
		iterator(const Deque& deque, block_cache<Node> curr, size_t curr_index) : deque(deque), curr(curr), curr_index(curr_index) {}

		bool operator==(const iterator& other) const { return curr == other.curr && curr_index == other.curr_index; }

		const T& operator*() {
			if (curr_index == curr.get().data.size()) {
				throw std::invalid_argument("cannot dereference end list iterator");
			}
			return curr.get().data[curr_index];
		}

		iterator& operator++() {
			if (curr_index == curr.get().data.size()) {
				throw std::invalid_argument("cannot increment end list iterator");
			}
			curr_index++;
			if (curr_index == curr.get().data.size() && curr != deque.tail) {
				curr = curr.get().next;
				curr_index = 0;
			}
			return *this;
		}

		iterator& operator--() {
			if (curr_index == 0) {
				if (curr == deque.head) {
					throw std::invalid_argument("cannot decrement begin list iterator");
				}
				curr = curr.get().prev;
				curr_index = curr.get().data.size();
			}
			curr_index--;
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

public:
	void clear() {
		if (empty()) {
			return;
		}
		head.set(Node(head, head, std::vector<T>()));
		tail = head;
	}

	template <class... Args>
	iterator emplace_back(Args&&... args) {
		if (tail.get().data.size() < block_limit) {
			tail.update([&](Node& n) { n.data.emplace_back(std::forward<Args>(args)...); });
		} else {
			tail = block_manager.transaction([&]() {
				block_cache<Node> new_node(std::in_place, head, tail, [&] { std::vector<T> data; data.emplace_back(std::forward<Args>(args)...); return data; }());
				if (head == tail) {
					head.update([&](Node& n) { n.next = n.prev = new_node; });
				} else {
					head.update([&](Node& n) { n.prev = new_node; });
					tail.update([&](Node& n) { n.next = new_node; });
				}
				return new_node;
			});
		}
		return iterator(*this, tail, tail.get().data.size() - 1);
	}

	template <class... Args>
	iterator emplace_front(Args&&... args) {
		if (head.get().data.size() < block_limit) {
			head.update([&](Node& n) { n.data.emplace(n.data.begin(), std::forward<Args>(args)...); });
		} else {
			block_manager.transaction([&]() {
				block_cache<Node> next(head.get().next);
				block_cache_lazy<Node> new_node;
				new_node.set(next, head, [&] {
					std::vector<T> data;
					data.emplace(data.begin(), std::forward<Args>(args)...);
					if (head == next) {
						head.update([&](Node& n) { std::swap(n.data, data); n.next = n.prev = new_node; });
					} else {
						head.update([&](Node& n) { std::swap(n.data, data); n.next = new_node; });
						next.update([&](Node& n) { n.prev = new_node; });
					}
					return data;
				}());
			});
		}
		return iterator(*this, head, 0);
	}

	void pop_back() {
		if (tail.get().data.size() == 0) {
			throw std::invalid_argument("deque is empty");
		}
		if (tail.get().data.size() > 1 || head == tail) {
			tail.update([&](Node& n) { n.data.pop_back(); });
		} else {
			tail = block_manager.transaction([&]() {
				block_cache<Node> prev(tail.get().prev);
				prev.update([&](Node& n) { n.next = head; });
				head.update([&](Node& n) { n.prev = prev; });
				return prev;
			});
		}
	}

	void pop_front() {
		if (head.get().data.size() == 0) {
			throw std::invalid_argument("deque is empty");
		}
		if (head.get().data.size() > 1 || head == tail) {
			head.update([](Node& n) { n.data.erase(n.data.begin()); });
		} else {
			block_manager.transaction([&]() {
				block_cache<Node> next(head.get().next);
				block_cache<Node> nnext(next.get().next);
				if (head == nnext) {
					head.update([&](Node& n) { n.next = n.prev = head; next.update([&](Node& nn) { std::swap(n.data, nn.data); }); });
				} else {
					head.update([&](Node& n) { n.next = nnext; next.update([&](Node& nn) { std::swap(n.data, nn.data); }); });
					nnext.update([&](Node& n) { n.prev = head; });
				}
			});
		}
	}
};


template<class T> requires (deque_block_limit<T> <= 1)
class Deque<T> : public List<T> {
public:
	using List<T>::List;
};


END_NAMESPACE(BlockStore)