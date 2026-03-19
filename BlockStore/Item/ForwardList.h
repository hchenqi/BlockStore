#include "../data/cache.h"


namespace BlockStore {


template<class T>
struct ForwardListNode {
	block<ForwardListNode> next;
	T value;

	ForwardListNode() = default;
	ForwardListNode(const block<ForwardListNode>& next, auto&&... args) : next(next), value(std::forward<decltype(args)>(args)...) {}

	friend constexpr auto layout(layout_type<ForwardListNode>) { return declare(&ForwardListNode::next, &ForwardListNode::value); }
};


template<class T, class CacheType>
class ForwardList {
private:
	using Node = ForwardListNode<T>;

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
		block_view<Node, CacheType> node;

	private:
		value_wrapper(const block_view<Node, CacheType>& node) : node(node) {}

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
		const block_view_local<Sentinel>* root;
		block_view_lazy<Node, CacheType> curr;

	private:
		iterator(const block_view_local<Sentinel>& root, block_view_lazy<Node, CacheType> curr) : root(&root), curr(std::move(curr)) {}

	public:
		bool operator==(const iterator& other) const { return curr == other.curr; }

		value_wrapper operator*() {
			if (curr == *root) {
				throw std::invalid_argument("cannot dereference end forward_list iterator");
			}
			return block_view<Node, CacheType>(curr);
		}

		const T* operator->() {
			if (curr == *root) {
				throw std::invalid_argument("cannot dereference end forward_list iterator");
			}
			return &curr.get().value;
		}

		iterator& operator++() {
			curr = curr == *root ? root->get().next : curr.get().next;
			return *this;
		}
	};

public:
	ForwardList(CacheType& cache, block_ref root) : cache(cache), root(BlockCacheLocal<Sentinel>::read(std::move(root), [=] { return Sentinel(root); })) {}

protected:
	CacheType& cache;

private:
	block_view_local<Sentinel> root;

public:
	bool empty() const { return root.get().next == root; }

	iterator before_begin() const { return iterator(root, cache.read_lazy(root)); }
	iterator begin() const { return iterator(root, cache.read_lazy(root.get().next)); }
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
		root.set(Sentinel(root));
	}

	iterator emplace_front(auto&&... args) {
		return cache.transaction([&] {
			block_view<Node, CacheType> new_node = cache.create(root.get().next, std::forward<decltype(args)>(args)...);
			root.update([&](Sentinel& r) { r.next = new_node; });
			return iterator(root, std::move(new_node));
		});
	}

	iterator emplace_after(iterator pos, auto&&... args) {
		if (pos == before_begin()) {
			return emplace_front(std::forward<decltype(args)>(args)...);
		}
		return cache.transaction([&] {
			block_view<Node, CacheType> new_node = cache.create(pos.curr.get().next, std::forward<decltype(args)>(args)...);
			pos.curr.update([&](Node& n) { n.next = new_node; });
			return iterator(root, std::move(new_node));
		});
	}

	iterator pop_front() {
		if (empty()) {
			throw std::invalid_argument("forward_list is empty");
		}
		return cache.transaction([&] {
			block_view<Node, CacheType> next = cache.read(root.get().next);
			root.update([&](Sentinel& r) { r.next = next.get().next; });
			return iterator(root, cache.read_lazy(next.get().next));
		});
	}

	iterator erase_after(iterator pos) {
		if (pos == before_begin()) {
			return pop_front();
		}
		if (pos.curr.get().next == root) {
			throw std::invalid_argument("forward_list erase iterator outside range");
		}
		return cache.transaction([&] {
			block_view<Node, CacheType> next = cache.read(pos.curr.get().next);
			pos.curr.update([&](Node& n) { n.next = next.get().next; });
			return iterator(root, cache.read_lazy(next.get().next));
		});
	}
};


} // namespace BlockStore
