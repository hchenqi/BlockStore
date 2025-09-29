#include "block_cache.h"
#include "block_manager.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
class List {
public:
	struct Node {
		block<Node> next;
		block<Node> prev;
		T value;

		Node() = default;
		Node(block<Node> next, block<Node> prev, T value) : next(next), prev(prev), value(value) {}
	};
	friend auto layout(layout_type<Node>) { return declare(&Node::next, &Node::prev, &Node::value); }

	struct Sentinel {
		block<Node> next;
		block<Node> prev;

		Sentinel() = default;
		Sentinel(block_ref initial) : next(initial), prev(initial) {}
		Sentinel(block<Node> next, block<Node> prev) : next(next), prev(prev) {}
	};
	friend auto layout(layout_type<Sentinel>) { return declare(&Sentinel::next, &Sentinel::prev); }

public:
	class iterator {
	private:
		block_cache_lazy<Node> curr;
		const block<Node> end;

	public:
		iterator(block<Node> curr, block<Node> end) : curr(curr), end(end) {}

		bool operator==(const iterator& other) const { return curr.ref() == other.curr.ref(); }
		bool operator!=(const iterator& other) const { return curr.ref() != other.curr.ref(); }

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

	void push_back(T value) {
		block_manager.transaction([&]() {
			block<Node> new_node;
			new_node.write(Node(root.ref(), root.get().prev, std::move(value)));
			if (empty()) {
				root.update([&](Sentinel& r) { r.next = r.prev = new_node; });
			} else {
				block_cache<Node> back(root.get().prev);
				back.update([&](Node& n) { n.next = new_node; });
				root.update([&](Sentinel& r) { r.prev = new_node; });
			}
		});
	}

	T pop_back() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		block_cache<Node> back(root.get().prev);
		block_manager.transaction([&]() {
			if (back.get().prev == root.ref()) {
				root.update([&](Sentinel& r) { r.next = r.prev = root.ref(); });
			} else {
				block_cache<Node> prev(back.get().prev);
				prev.update([&](Node& n) { n.next = root.ref(); });
				root.update([&](Sentinel& r) { r.prev = prev.ref(); });
			}
		});
		return back.get().value;
	}

	void push_front(T value) {
		block_manager.transaction([&]() {
			block<Node> new_node;
			new_node.write(Node(root.get().next, root.ref(), std::move(value)));
			if (empty()) {
				root.update([&](Sentinel& r) { r.next = r.prev = new_node; });
			} else {
				block_cache<Node> front(root.get().next);
				front.update([&](Node& n) { n.prev = new_node; });
				root.update([&](Sentinel& r) { r.next = new_node; });
			}
		});
	}

	T pop_front() {
		if (empty()) {
			throw std::invalid_argument("list is empty");
		}
		block_cache<Node> front(root.get().next);
		block_manager.transaction([&]() {
			if (front.get().next == root.ref()) {
				root.update([&](Sentinel& r) { r.next = r.prev = root.ref(); });
			} else {
				block_cache<Node> next(front.get().next);
				next.update([&](Node& n) { n.prev = root.ref(); });
				root.update([&](Sentinel& r) { r.next = next.ref(); });
			}
		});
		return front.get().value;
	}
};


END_NAMESPACE(BlockStore)