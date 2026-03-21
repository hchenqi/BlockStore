#pragma once

#include "../data/cache.h"
#include "CppSerialize/stl/optional.h"
#include "CppSerialize/stl/vector.h"

#include <optional>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <vector>


namespace BlockStore {


template<class KeyType, class ValueType, class NodeCache, class KeyCache, class Comp = std::less<KeyType>>
class OrderedRefMap {
private:
	using Meta = std::pair<std::optional<block<Node>>, size_t>;
	using NodeEntry = std::pair<KeyType, block_ref>;
	using Node = std::vector<NodeEntry>;
	using LeafEntry = std::conditional_t<std::is_void_v<ValueType>, KeyType, std::pair<KeyType, ValueType>>;
	using Leaf = std::vector<LeafEntry>;

private:
	class node_iterator {
	public:
		node_iterator(NodeCache& cache) : cache(&cache) {}

	protected:
		NodeCache* cache;
		std::vector<std::pair<size_t, block_view<Node, NodeCache>>> pos;

	public:
		bool empty() const {
			return pos.empty();
		}

		bool operator==(const node_iterator& other) const {
			return pos == other.pos;
		}

		const Node& get() const {
			if (empty()) {
				throw std::invalid_argument("empty node_iterator");
			}
			return pos.back().second.get();
		}

	public:
		node_iterator& clear() {
			pos.clear();
			return *this;
		}

		node_iterator& root(block_ref ref) {
			pos.clear();
			pos.emplace_back(0, cache->read(std::move(ref)));
			return *this;
		}

		node_iterator& child(size_t index) {
			if (empty()) {
				throw std::invalid_argument("empty node_iterator");
			}
			pos.emplace_back(index, cache->read(get()[index].second));
			return *this;
		}

		node_iterator& parent() {
			if (empty()) {
				throw std::invalid_argument("empty node_iterator");
			}
			pos.pop_back();
			return *this;
		}

		node_iterator& next() {
			size_t level = 0;
			size_t index;
			do {
				if (empty()) {
					return *this;
				}
				index = pos.back().first + 1;
				pos.pop_back();
				level++;
			} while (index >= get().size());
			while (level--) {
				child(index);
				index = 0;
			}
			return *this;
		}

		node_iterator& prev() {
			size_t level = 0;
			size_t index;
			do {
				if (empty()) {
					return *this;
				}
				index = pos.back().first;
				pos.pop_back();
				level++;
			} while (index == 0);
			while (level--) {
				child(index - 1);
				index = get().size();
			}
			return *this;
		}
	};

public:
	class iterator : private node_iterator {
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = LeafEntry;
		using difference_type = std::ptrdiff_t;
		using pointer = const value_type*;
		using reference = const value_type&;

	private:
		friend class OrderedRefMap;

	private:
		iterator(node_iterator it, size_t index) : node_iterator(std::move(it)), index(index) {}

	private:
		size_t index;

	public:
		LeafEntry operator*() {
			return get()[index];
		}

		const LeafEntry* operator->() {
			return &get()[index];
		}

		iterator& operator++() {
			index++;
			if (index == get().size()) {
				next();
				index = 0;
			}
			return *this;
		}

		iterator& operator+=(size_t offset) {
			for (index += offset;;) {
				size_t size = get().size();
				if (index >= size) {
					next();
					index -= size;
				} else {
					break;
				}
			};
			return *this;
		}
	};

public:
	OrderedRefMap(NodeCache& node_cache, KeyCache& key_cache, ValueCache& value_cache, block_ref ref) :
		node_cache(node_cache), key_cache(key_cache), value_cache(value_cache),
		meta(BlockCacheLocal<Meta>::read(std::move(ref))) {}

private:
	block_view_local<Meta> meta;

private:
	NodeCache& node_cache;
	KeyCache& key_cache;
	ValueCache& value_cache;

private:
	bool empty() const { return !meta.get().first.has_value(); }
	size_t depth() const { return meta.get().second; }

private:
	node_iterator node_root() const {
		node_iterator it(node_cache);
		it.root(meta.get().first.value());
		return it;
	}
	node_iterator node_front() const {
		if (empty()) {
			return node_end();
		}
		node_iterator it = node_root();
		size_t level = depth();
		while (level--) {
			it.child(0);
		}
		return it;
	}
	node_iterator node_back() const {
		if (empty()) {
			return node_end();
		}
		node_iterator it = node_root();
		size_t level = depth();
		while (level--) {
			it.child(it.get().size() - 1);
		}
		return it;
	}
	node_iterator node_end() const {
		return node_iterator(node_cache);
	}

public:
	iterator begin() const { return iterator(node_front(), 0); }
	iterator end() const { return iterator(node_end(), 0); }

public:
	template<class Key
	iterator lower_bound(const Key& key) const {
		if (empty()) {
			return end();
		}
		node_iterator it = node_root();
		size_t level = depth();
		while (level--) {
			const Node& node = it.get();
			size_t index = std::lower_bound(node.begin(), node.end(), key, [&](const NodeEntry& entry, const Key& key) { return Comp()(key_cache.read(entry.first).get(), key); }) - node.begin();
			it.child(index);
		}

		auto [node_ref, node, idx] = locate_leaf(key);
		if (!is_valid(node_ref)) {
			return std::nullopt;
		}
		if (idx < node.keys.size() && compare_keys(node.keys[idx], key) == 0) {
			return node.values[idx];
		}
		return std::nullopt;
	}

	void insert(Entry entry) {
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
			throw std::runtime_error("OrderedRefMap internal node malformed");
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
