#include "block_cache.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
class List {
private:
	struct Node {
		block<Node> next;
		block<Node> prev;
		T value;

		Node() = default;
		Node(const block<Node>& next, const block<Node>& prev, auto&&... args) : next(next), prev(prev), value(std::forward<decltype(args)>(args)...) {}

		friend constexpr auto layout(layout_type<Node>) { return declare(&Node::next, &Node::prev, &Node::value); }
	};

	struct Sentinel {
		block<Node> next;
		block<Node> prev;

		Sentinel() = default;
		Sentinel(const block_ref& root) : next(root), prev(root) {}
		Sentinel(const block<Node>& next, const block<Node>& prev) : next(next), prev(prev) {}

		friend constexpr auto layout(layout_type<Sentinel>) { return declare(&Sentinel::next, &Sentinel::prev); }
	};

public:
	class value_wrapper {
	private:
		friend class List;

	private:
		block_cache<Node> node;

	private:
		value_wrapper(const block_cache<Node>& node) : node(node) {}

	public:
		const T& get() const { return node.get().value; }
		const T& set(auto&&... args) { return update([&](T& object) { object = T(std::forward<decltype(args)>(args)...); }); }
		const T& update(auto f) { return node.update([&](Node& node) { f(node.value); }).value; }

		operator const T& () const { return get(); }
		const T* operator->() const { return &get(); }
	};

	class iterator {
	private:
		friend class List;

	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T*;
		using reference = T&;

	private:
		block_cache<Sentinel> root;
		block_cache_lazy<Node> curr;

	private:
		iterator(const block_cache<Sentinel>& root, const block<Node>& curr) : root(root), curr(curr) {}

	public:
		bool operator==(const iterator& other) const { return curr == other.curr; }

		value_wrapper operator*() {
			if (curr == root) {
				throw std::invalid_argument("cannot dereference end list iterator");
			}
			return block_cache<Node>(static_cast<block<Node>>(curr));
		}

		const T* operator->() {
			return (**this).operator->();
		}

		iterator& operator++() {
			if (curr == root) {
				throw std::invalid_argument("cannot increment end list iterator");
			}
			curr = curr.get().next;
			return *this;
		}

		iterator& operator+=(size_t offset) {
			while (offset--) { ++*this; };
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

		iterator& operator-=(size_t offset) {
			while (offset--) { --*this; };
			return *this;
		}
	};

public:
	List(const block_ref& root) : root(root, [&] { return Sentinel(root); }) {}

private:
	block_cache<Sentinel> root;

public:
	bool empty() const { return root.get().next == root; }

	iterator begin() const { return iterator(root, root.get().next); }
	iterator end() const { return iterator(root, root); }

	std::reverse_iterator<iterator> rbegin() const { return std::reverse_iterator<iterator>(end()); }
	std::reverse_iterator<iterator> rend() const { return std::reverse_iterator<iterator>(begin()); }

	value_wrapper front() const {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		return *begin();
	}

	value_wrapper back() const {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		return *--end();
	}

public:
	void clear() {
		if (empty()) {
			return;
		}
		root.set(Sentinel(root));
	}

	iterator emplace_back(auto&&... args) {
		return block_manager.transaction([&] {
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
		return block_manager.transaction([&] {
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
		return block_manager.transaction([&] {
			block_cache<Node> prev(pos.curr.get().prev);
			block_cache<Node> new_node(std::in_place, pos.curr, prev, std::forward<decltype(args)>(args)...);
			prev.update([&](Node& n) { n.next = new_node; });
			pos.curr.update([&](Node& n) { n.prev = new_node; });
			return iterator(root, new_node);
		});
	}

	iterator pop_back() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		return block_manager.transaction([&] {
			block_cache<Node> back(root.get().prev);
			if (back.get().prev == root) {
				root.update([&](Sentinel& r) { r.next = r.prev = root; });
			} else {
				block_cache<Node> prev(back.get().prev);
				prev.update([&](Node& n) { n.next = root; });
				root.update([&](Sentinel& r) { r.prev = prev; });
			}
			return end();
		});
	}

	iterator pop_front() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		return block_manager.transaction([&] {
			block_cache<Node> front(root.get().next);
			if (front.get().next == root) {
				root.update([&](Sentinel& r) { r.next = r.prev = root; });
				return end();
			} else {
				block_cache<Node> next(front.get().next);
				next.update([&](Node& n) { n.prev = root; });
				root.update([&](Sentinel& r) { r.next = next; });
				return iterator(root, next);
			}
		});
	}

	iterator erase(iterator pos) {
		if (pos == end()) {
			throw std::invalid_argument("list erase iterator outside range");
		}
		if (pos.curr.get().next == root) {
			return pop_back();
		}
		if (pos == begin()) {
			return pop_front();
		}
		return block_manager.transaction([&] {
			block_cache<Node> prev(pos.curr.get().prev);
			block_cache<Node> next(pos.curr.get().next);
			prev.update([&](Node& n) { n.next = next; });
			next.update([&](Node& n) { n.prev = prev; });
			return iterator(root, next);
		});
	}
};


END_NAMESPACE(BlockStore)