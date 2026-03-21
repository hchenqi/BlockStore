#pragma once

#include "../data/cache.h"
#include "CppSerialize/stl/vector.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>


namespace BlockStore {


template<class Key, class Value, class NodeCache, class KeyCache, class ValueCache, class Comp = std::less<Key>>
class OrderedRefMap {
protected:
	using Meta = std::pair<block<Node>, size_t>;  // { root, depth }
	using Entry = std::pair<block_ref, block_ref>;  // { key, value }
	using Node = std::vector<Entry>;

private:
	class node_iterator {
	public:
		node_iterator(block_view<Node, NodeCache> root) { pos.emplace_back(0, std::move(root)); }

	protected:
		std::vector<std::pair<size_t, block_view<Node, NodeCache>>> pos;

	public:
		bool is_root() const {
			return pos.size() == 1;
		}

		bool is_begin() const {
			for (const auto& [view, index] : reverse(pos)) {
				if (index > 0) {
					return false;
				}
			}
			return true;
		}

		bool is_end() const {
			const auto& [view, index] = pos.back();
			return index == view.get().size();
		}

	public:
		

	public:
		void parent() {
			if (is_root()) {
				throw std::invalid_argument("getting parent of root node");
			}
			pos.pop_back();
		}

		void begin() {
			auto& [view, index] = pos.back();
			index = 0;
		}

		void back() {
			auto& [view, index] = pos.back();
			index = view.get().size() - 1;
		}

		void end() {
			auto& [view, index] = pos.back();
			index = view.get().size();
		}

		void child_begin() {
			const auto& [view, index] = pos.back();
			pos.emplace_back(cache.read(view.get()[index].second), 0);
		}

		void child_back() {
			const auto& [view, index] = pos.back();
			pos.emplace_back(cache.read(view.get()[index].second), view.get().size() - 1);
		}

		void child_end() {
			const auto& [view, index] = pos.back();
			pos.emplace_back(cache.read(view.get()[index].second), view.get().size());
		}

		void next(size_t offset = 1) {
			if (offset == 0) {
				return;
			}
			auto& [view, index] = pos.back();
			if (index + offset <= view.get().size()) {
				index += offset;
				if (index == view.get().size()) {
					try {
						parent();
						next(1);
						child_begin();
					} catch (...) {}
				}
			} else {
				parent();
				next(1);
				child_begin();
				next(index + offset - view.get().size());
			}
		}

		void prev(size_t offset = 1) {
			if (offset == 0) {
				return;
			}
			auto& [view, index] = pos.back();
			if (index >= offset) {
				index -= offset;
			} else {
				parent();
				prev(1);
				child_end();
				prev(offset - index);
			}
		}
	};

public:
	class iterator : private node_iterator {
	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = Entry;
		using difference_type = std::ptrdiff_t;
		using pointer = const value_type*;
		using reference = const value_type&;

	private:
		friend class OrderedRefMap;

	private:
		iterator(node_iterator it) : node_iterator(std::move(it)) {}

	public:
		Entry operator*() {
			const auto& [view, index] = pos.back();
			return view.get()[index];
		}

		iterator& operator++() {
			node_iterator::next();
			return *this;
		}

		iterator& operator+=(size_t offset) {
			node_iterator::next(offset);
			return *this;
		}

		iterator& operator--() {
			node_iterator::prev();
			return *this;
		}

		iterator& operator-=(size_t offset) {
			node_iterator::prev(offset);
			return *this;
		}
	};

public:
	OrderedMap(NodeCache& node_cache, KeyCache& key_cache, ValueCache& value_cache, block_ref ref) : node_cache(node_cache), meta(BlockCacheLocal<Meta>::read(std::move(ref), [&] { return std::make_pair(cache.create(), 0); })) {}

private:
	block_view_local<Meta> meta;

private:
	NodeCache& node_cache;
	KeyCache& key_cache;
	ValueCache& value_cache;

private:
	size_t depth() const { return meta.get().second; }

	node_iterator root() const { node_cache.read(meta.get().first); }

private:


public:
	bool empty() const {
		return root().is_end();
	}

	iterator begin() const {
		node_iterator it = root();
		for (size_t i = 1, d = depth(); i <= d; ++i) {
			it.child_begin();
		}
		return it;
	}

	iterator end() const {
		node_iterator it = root();
		it.end();
		return it;
	}

public:
	std::optional<block_ref> lower_bound(const Key& key) const {
		node_iterator it = root();



		auto [node_ref, node, idx] = locate_leaf(key);
		if (!is_valid(node_ref)) {
			return std::nullopt;
		}
		if (idx < node.keys.size() && compare_keys(node.keys[idx], key) == 0) {
			return node.values[idx];
		}
		return std::nullopt;
	}

	void insert(const block_ref& key, const block_ref& value) {
		if (!is_valid(root)) {
			root = create_node(true);
		}

		auto res = insert_internal(root, key, value);
		if (res) {
			// split root
			block_ref new_root = create_node(false);
			Node root_node;
			root_node.leaf = false;
			root_node.keys.push_back(res->first);
			root_node.values.push_back(root);
			root_node.values.push_back(res->second);
			write_node(new_root, root_node);
			root = new_root;
		}
	}

protected:
	Derived& derived() { return static_cast<Derived&>(*this); }

	const Derived& derived() const { return static_cast<const Derived&>(*this); }

	Node read_node(const block_ref& ref) const {
		return derived().read_node(ref);
	}

	void write_node(const block_ref& ref, const Node& node) const {
		derived().write_node(ref, node);
	}

	block_ref create_node(bool leaf) {
		return derived().create_node(leaf);
	}

	int compare_keys(const block_ref& a, const block_ref& b) const {
		return derived().compare_keys(a, b);
	}

protected:
	static bool is_valid(const block_ref& ref) {
		try {
			ref.get_manager();
			return true;
		} catch (const std::invalid_argument&) {
			return false;
		}
	}

	static bool ref_equal(const block_ref& a, const block_ref& b) {
		const bool a_valid = is_valid(a);
		const bool b_valid = is_valid(b);
		if (a_valid != b_valid) {
			return false;
		}
		if (!a_valid) {
			return true;
		}
		return static_cast<ref_t>(a) == static_cast<ref_t>(b);
	}

private:
	block_ref root;

	// Read a node; if the underlying block is empty (uninitialized), return an empty leaf node.
	Node read_node_safe(const block_ref& ref) const {
		if (!is_valid(ref)) {
			return Node();
		}
		try {
			return read_node(ref);
		} catch (const std::invalid_argument&) {
			Node n;
			n.leaf = true;
			return n;
		}
	}

	bool node_overflow(const Node& node) const {
		try {
			BlockSize(node).Get();
			return false;
		} catch (const std::invalid_argument&) {
			return true;
		}
	}

	// Returns tuple of (leaf_node_ref, leaf_node, index) where index is the lower_bound position for key.
	std::tuple<block_ref, Node, size_t> locate_leaf(const block_ref& key) const {
		block_ref current = root;
		Node node = read_node_safe(current);

		while (!node.leaf) {
			auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key,
									   [&](const block_ref& a, const block_ref& b) { return compare_keys(a, b) < 0; });
			size_t idx = it - node.keys.begin();
			if (idx >= node.values.size()) {
				// malformed internal node
				return { block_ref(), Node(), 0 };
			}
			current = node.values[idx];
			node = read_node_safe(current);
		}

		auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key,
								   [&](const block_ref& a, const block_ref& b) { return compare_keys(a, b) < 0; });
		size_t idx = it - node.keys.begin();
		return { current, std::move(node), idx };
	}

	// Inserts into the subtree rooted at `node_ref`.
	// If the node splits, returns (promoted_key, new_node_ref) for the parent.
	std::optional<std::pair<block_ref, block_ref>> insert_internal(const block_ref& node_ref,
																   const block_ref& key, const block_ref& value) {

		Node node = read_node_safe(node_ref);

		if (node.leaf) {
			auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key,
									   [&](const block_ref& a, const block_ref& b) { return compare_keys(a, b) < 0; });
			size_t idx = it - node.keys.begin();

			if (idx < node.keys.size() && compare_keys(node.keys[idx], key) == 0) {
				node.values[idx] = value;
				write_node(node_ref, node);
				return std::nullopt;
			}

			node.keys.insert(node.keys.begin() + idx, key);
			node.values.insert(node.values.begin() + idx, value);

			if (node_overflow(node)) {
				// Split leaf node
				size_t mid = node.keys.size() / 2;
				Node right;
				right.leaf = true;
				right.keys.assign(node.keys.begin() + mid, node.keys.end());
				right.values.assign(node.values.begin() + mid, node.values.end());
				node.keys.erase(node.keys.begin() + mid, node.keys.end());
				node.values.erase(node.values.begin() + mid, node.values.end());

				right.next = node.next;
				block_ref right_ref = create_node(true);
				write_node(right_ref, right);

				node.next = right_ref;
				write_node(node_ref, node);

				return std::make_pair(right.keys.front(), right_ref);
			}

			write_node(node_ref, node);
			return std::nullopt;
		}

		// Internal node
		auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key,
								   [&](const block_ref& a, const block_ref& b) { return compare_keys(a, b) < 0; });
		size_t idx = it - node.keys.begin();
		if (idx >= node.values.size()) {
			throw std::runtime_error("OrderedMap internal node malformed");
		}

		auto child_split = insert_internal(node.values[idx], key, value);
		if (!child_split) {
			return std::nullopt;
		}

		auto [promoted_key, new_child_ref] = *child_split;
		auto insert_pos = std::lower_bound(node.keys.begin(), node.keys.end(), promoted_key,
										   [&](const block_ref& a, const block_ref& b) { return compare_keys(a, b) < 0; });
		size_t insert_idx = insert_pos - node.keys.begin();

		node.keys.insert(node.keys.begin() + insert_idx, promoted_key);
		node.values.insert(node.values.begin() + insert_idx + 1, new_child_ref);

		if (node_overflow(node)) {
			// Split internal node
			size_t mid = node.keys.size() / 2;
			block_ref promote = node.keys[mid];

			Node right;
			right.leaf = false;
			right.keys.assign(node.keys.begin() + mid + 1, node.keys.end());
			right.values.assign(node.values.begin() + mid + 1, node.values.end());

			node.keys.erase(node.keys.begin() + mid, node.keys.end());
			node.values.erase(node.values.begin() + mid + 1, node.values.end());

			block_ref right_ref = create_node(false);
			write_node(right_ref, right);
			write_node(node_ref, node);
			return std::make_pair(promote, right_ref);
		}

		write_node(node_ref, node);
		return std::nullopt;
	}
};


} // namespace BlockStore
