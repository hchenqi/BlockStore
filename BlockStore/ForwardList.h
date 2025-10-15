#include "block_cache.h"
#include "block_manager.h"


BEGIN_NAMESPACE(BlockStore)


template<class T>
class ForwardList {
private:
	struct Node {
		block<Node> next;
		T value;

		Node() = default;
		Node(const block<Node>& next, auto&&... args) : next(next), value(std::forward<decltype(args)>(args)...) {}

		friend constexpr auto layout(layout_type<Node>) { return declare(&Node::next, &Node::value); }
	};

	struct Sentinel {
		block<Node> next;

		Sentinel() = default;
		Sentinel(const block_ref& root) : next(root) {}
		Sentinel(const block<Node>& next) : next(next) {}

		friend constexpr auto layout(layout_type<Sentinel>) { return declare(&Sentinel::next); }
	};

public:
	class value_wrapper {
	private:
		friend class ForwardList;

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
		friend class ForwardList;

	public:
		using iterator_category = std::forward_iterator_tag;
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
				throw std::invalid_argument("cannot dereference end forward_list iterator");
			}
			return block_cache<Node>(static_cast<block<Node>>(curr));
		}

		const T* operator->() {
			return (**this).operator->();
		}

		iterator& operator++() {
			curr = curr == root ? root.get().next : curr.get().next;
			return *this;
		}
	};

public:
	ForwardList(block<Sentinel> root) : root(root, [&] { return Sentinel(root); }) {}

private:
	block_cache<Sentinel> root;

public:
	bool empty() const { return root.get().next == root; }

	iterator before_begin() const { return iterator(root, root); }
	iterator begin() const { return iterator(root, root.get().next); }
	iterator end() const { return before_begin(); }

	value_wrapper front() const {
		if (empty()) {
			throw std::invalid_argument("forward_list is empty");
		}
		return *begin();
	}

public:
	void clear() {
		if (empty()) {
			return;
		}
		block_manager.transaction([&] {
			root.set(Sentinel(root));
		});
	}

	iterator emplace_front(auto&&... args) {
		return block_manager.transaction([&] {
			block_cache<Node> new_node(std::in_place, root.get().next, std::forward<decltype(args)>(args)...);
			root.update([&](Sentinel& r) { r.next = new_node; });
			return iterator(root, new_node);
		});
	}

	iterator emplace_after(iterator pos, auto&&... args) {
		if (pos == before_begin()) {
			return emplace_front(std::forward<decltype(args)>(args)...);
		}
		return block_manager.transaction([&] {
			block_cache<Node> new_node(std::in_place, pos.curr.get().next, std::forward<decltype(args)>(args)...);
			pos.curr.update([&](Node& n) { n.next = new_node; });
			return iterator(root, new_node);
		});
	}

	iterator pop_front() {
		if (empty()) {
			throw std::invalid_argument("forward_list is empty");
		}
		return block_manager.transaction([&] {
			block_cache<Node> next(root.get().next);
			root.update([&](Sentinel& r) { r.next = next.get().next; });
			return iterator(root, next.get().next);
		});
	}

	iterator erase_after(iterator pos) {
		if (pos == before_begin()) {
			return pop_front();
		}
		if (pos.curr.get().next == root) {
			throw std::invalid_argument("forward_list erase iterator outside range");
		}
		return block_manager.transaction([&] {
			block_cache<Node> next(pos.curr.get().next);
			pos.curr.update([&](Node& n) { n.next = next.get().next; });
			return iterator(root, next.get().next);
		});
	}
};


END_NAMESPACE(BlockStore)