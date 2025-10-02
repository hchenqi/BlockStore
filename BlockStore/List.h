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

		template <class... Args>
		Node(block<Node> next, block<Node> prev, Args&&... args) : next(next), prev(prev), value(std::forward<Args>(args)...) {}
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
		block_cache_lazy<Node> curr;
		const block<Node> end;

	public:
		iterator(block<Node> curr, block<Node> end) : curr(curr), end(end) {}

		bool operator==(const iterator& other) const { return curr.ref() == other.curr.ref(); }

		const T& operator*() {
			if (curr.ref() == end) {
				throw std::invalid_argument("cannot dereference end list iterator");
			}
			return curr.get().value;
		}

		iterator& operator++() {
			if (curr.ref() == end) {
				throw std::invalid_argument("cannot increment end list iterator");
			}
			curr = curr.get().next;
			return *this;
		}

		iterator& operator--() {
			block<Node> prev = curr.ref() == end ? block<Sentinel>(curr.ref()).read().prev : curr.get().prev;
			if (prev == end) {
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
	bool empty() const { return root.get().prev == root.ref(); }

	iterator begin() const { return iterator(root.get().next, root.ref()); }
	iterator end() const { return iterator(root.ref(), root.ref()); }

public:
	void clear() {
		if (empty()) {
			return;
		}
		root.set(Sentinel(root.ref(), root.ref()));
	}

	template <class... Args>
	iterator emplace_back(Args&&... args) {
		return block_manager.transaction([&]() {
			block<Node> new_node;
			new_node.write(Node(root.ref(), root.get().prev, std::forward<Args>(args)...));
			if (empty()) {
				root.update([&](Sentinel& r) { r.next = r.prev = new_node; });
			} else {
				block_cache<Node> back(root.get().prev);
				back.update([&](Node& n) { n.next = new_node; });
				root.update([&](Sentinel& r) { r.prev = new_node; });
			}
			return iterator(new_node, root.ref());
		});
	}

	template <class... Args>
	iterator emplace_front(Args&&... args) {
		return block_manager.transaction([&]() {
			block<Node> new_node;
			new_node.write(Node(root.get().next, root.ref(), std::forward<Args>(args)...));
			if (empty()) {
				root.update([&](Sentinel& r) { r.next = r.prev = new_node; });
			} else {
				block_cache<Node> front(root.get().next);
				front.update([&](Node& n) { n.prev = new_node; });
				root.update([&](Sentinel& r) { r.next = new_node; });
			}
			return iterator(new_node, root.ref());
		});
	}

	template <class... Args>
	iterator emplace(iterator pos, Args&&... args) {
		if (pos == end()) {
			return emplace_back(std::forward<Args>(args)...);
		}
		if (pos == begin()) {
			return emplace_front(std::forward<Args>(args)...);
		}
		return block_manager.transaction([&]() {
			block_cache<Node> prev(pos.curr.get().prev);
			block<Node> new_node;
			new_node.write(Node(pos.curr.ref(), prev.ref(), std::forward<Args>(args)...));
			prev.update([&](Node& n) { n.next = new_node; });
			pos.curr.update([&](Node& n) { n.prev = new_node; });
			return iterator(new_node, root.ref());
		});
	}

	void pop_back() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		block_manager.transaction([&]() {
			block_cache<Node> back(root.get().prev);
			if (back.get().prev == root.ref()) {
				root.update([&](Sentinel& r) { r.next = r.prev = root.ref(); });
			} else {
				block_cache<Node> prev(back.get().prev);
				prev.update([&](Node& n) { n.next = root.ref(); });
				root.update([&](Sentinel& r) { r.prev = prev.ref(); });
			}
		});
	}

	void pop_front() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		block_manager.transaction([&]() {
			block_cache<Node> front(root.get().next);
			if (front.get().next == root.ref()) {
				root.update([&](Sentinel& r) { r.next = r.prev = root.ref(); });
			} else {
				block_cache<Node> next(front.get().next);
				next.update([&](Node& n) { n.prev = root.ref(); });
				root.update([&](Sentinel& r) { r.next = next.ref(); });
			}
		});
	}

	void erase(iterator pos) {
		if (pos == end()) {
			throw std::invalid_argument("list erase iterator outside range");
		}
		if (pos.curr.get().next == root.ref()) {
			return pop_back();
		}
		if (pos == begin()) {
			return pop_front();
		}
		return block_manager.transaction([&]() {
			block_cache<Node> prev(pos.curr.get().prev);
			block_cache<Node> next(pos.curr.get().next);
			prev.update([&](Node& n) { n.next = next.ref(); });
			next.update([&](Node& n) { n.prev = prev.ref(); });
		});
	}
};


END_NAMESPACE(BlockStore)