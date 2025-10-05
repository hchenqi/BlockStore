#include "block_cache.h"
#include "block_manager.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
class List {
private:
	struct Node {
		block<Node> next;
		block<Node> prev;
		T value;

		Node() = default;

		Node(block<Node> next, block<Node> prev, auto&&... args) : next(next), prev(prev), value(std::forward<decltype(args)>(args)...) {}
	};
	friend constexpr auto layout(layout_type<Node>) { return declare(&Node::next, &Node::prev, &Node::value); }

	struct Sentinel {
		block<Node> next;
		block<Node> prev;

		Sentinel() = default;
		Sentinel(block_ref initial) : next(initial), prev(initial) {}
		Sentinel(block<Node> next, block<Node> prev) : next(next), prev(prev) {}
	};
	friend constexpr auto layout(layout_type<Sentinel>) { return declare(&Sentinel::next, &Sentinel::prev); }

public:
	class iterator {
	private:
		friend class List;

	private:
		const block_cache<Sentinel>& root;
		block_cache_lazy<Node> curr;

	public:
		iterator(const block_cache<Sentinel>& root, block_cache_lazy<Node> curr) : root(root), curr(curr) {}

		bool operator==(const iterator& other) const { return curr == other.curr; }

		const T& operator*() {
			if (curr == root) {
				throw std::invalid_argument("cannot dereference end list iterator");
			}
			return curr.get().value;
		}

		iterator& operator++() {
			if (curr == root) {
				throw std::invalid_argument("cannot increment end list iterator");
			}
			curr = curr.get().next;
			return *this;
		}

		iterator& operator--() {
			block<Node> prev = curr == root ? root.get().prev : curr.get().prev;
			if (prev == root) {
				throw std::invalid_argument("cannot decrement begin list iterator");
			}
			curr = prev;
			return *this;
		}
	};

public:
	List(block<Sentinel> root) : root(root, [&]() { return Sentinel(root); }) {}

private:
	block_cache<Sentinel> root;

public:
	bool empty() const { return root.get().prev == root; }

	iterator begin() const { return iterator(root, root.get().next); }
	iterator end() const { return iterator(root, static_cast<block<Node>>(root)); }

public:
	void clear() {
		if (empty()) {
			return;
		}
		root.set(Sentinel(root, root));
	}

	iterator emplace_back(auto&&... args) {
		return block_manager.transaction([&]() {
			block_cache<Node> new_node(std::in_place, root, root.get().prev, std::forward<decltype(args)>(args)...);
			if (empty()) {
				root.update([&](Sentinel& r) { r.next = r.prev = new_node; });
			} else {
				block_cache<Node> back(root.get().prev);
				back.update([&](Node& n) { n.next = new_node; });
				root.update([&](Sentinel& r) { r.prev = new_node; });
			}
			return iterator(root, new_node);
		});
	}

	iterator emplace_front(auto&&... args) {
		return block_manager.transaction([&]() {
			block_cache<Node> new_node(std::in_place, root.get().next, root, std::forward<decltype(args)>(args)...);
			if (empty()) {
				root.update([&](Sentinel& r) { r.next = r.prev = new_node; });
			} else {
				block_cache<Node> front(root.get().next);
				front.update([&](Node& n) { n.prev = new_node; });
				root.update([&](Sentinel& r) { r.next = new_node; });
			}
			return iterator(root, new_node);
		});
	}

	iterator emplace(iterator pos, auto&&... args) {
		if (pos == end()) {
			return emplace_back(std::forward<decltype(args)>(args)...);
		}
		if (pos == begin()) {
			return emplace_front(std::forward<decltype(args)>(args)...);
		}
		return block_manager.transaction([&]() {
			block_cache<Node> prev(pos.curr.get().prev);
			block_cache<Node> new_node(std::in_place, pos.curr, prev, std::forward<decltype(args)>(args)...);
			prev.update([&](Node& n) { n.next = new_node; });
			pos.curr.update([&](Node& n) { n.prev = new_node; });
			return iterator(root, new_node);
		});
	}

	void pop_back() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		block_manager.transaction([&]() {
			block_cache<Node> back(root.get().prev);
			if (back.get().prev == root) {
				root.update([&](Sentinel& r) { r.next = r.prev = root; });
			} else {
				block_cache<Node> prev(back.get().prev);
				prev.update([&](Node& n) { n.next = root; });
				root.update([&](Sentinel& r) { r.prev = prev; });
			}
		});
	}

	void pop_front() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		block_manager.transaction([&]() {
			block_cache<Node> front(root.get().next);
			if (front.get().next == root) {
				root.update([&](Sentinel& r) { r.next = r.prev = root; });
			} else {
				block_cache<Node> next(front.get().next);
				next.update([&](Node& n) { n.prev = root; });
				root.update([&](Sentinel& r) { r.next = next; });
			}
		});
	}

	void erase(iterator pos) {
		if (pos == end()) {
			throw std::invalid_argument("list erase iterator outside range");
		}
		if (pos.curr.get().next == root) {
			return pop_back();
		}
		if (pos == begin()) {
			return pop_front();
		}
		return block_manager.transaction([&]() {
			block_cache<Node> prev(pos.curr.get().prev);
			block_cache<Node> next(pos.curr.get().next);
			prev.update([&](Node& n) { n.next = next; });
			next.update([&](Node& n) { n.prev = prev; });
		});
	}
};


END_NAMESPACE(BlockStore)