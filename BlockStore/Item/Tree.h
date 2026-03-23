#pragma once

#include "../data/cache.h"
#include "CppSerialize/stl/vector.h"

#include <cassert>
#include <stdexcept>
#include <algorithm>


namespace BlockStore {


template<class Key>
using TreeNodeEntry = std::pair<Key, block_ref>;

template<class Key>
using TreeNodeKeys = std::vector<TreeNodeEntry<Key>>;

template<class Key>
using TreeNode = std::pair<block_ref, TreeNodeKeys<Key>>;

template<class Key, class Value>
using TreeLeafEntry = std::conditional_t<std::is_void_v<Value>, Key, std::pair<Key, Value>>;

template<class Key, class Value>
using TreeLeaf = std::vector<TreeLeafEntry<Key, Value>>;


template<class Key, class Value, class Comp, template<class T> class Cache>
class Tree {
private:
	using Meta = std::pair<block_ref, size_t>;
	using NodeEntry = TreeNodeEntry<Key>;
	using NodeKeys = TreeNodeKeys<Key>;
	using Node = TreeNode<Key>;
	using LeafEntry = TreeLeafEntry<Key, Value>;
	using Leaf = TreeLeaf<Key, Value>;

protected:
	using NodeCache = Cache<Node>;
	using LeafCache = Cache<Leaf>;

private:
	static NodeKeys& keys(Node& node) {
		return node.second;
	}
	static const NodeKeys& keys(const Node& node) {
		return node.second;
	}
	static const block_ref& child_ref(const Node& node, size_t index) {
		return index == 0 ? node.first : keys(node)[index - 1].second;
	}

private:
	static const Key& key(const NodeEntry& entry) {
		return entry.first;
	}
	static const Key& key(const LeafEntry& entry) {
		if constexpr (std::is_void_v<Value>) {
			return entry;
		} else {
			return entry.first;
		}
	}

private:
	class node_iterator {
	private:
		friend class Tree;
	private:
		node_iterator(NodeCache& node_cache, block_ref root) : node_cache(&node_cache), pos() { pos.emplace_back(0, node_cache.read(std::move(root))); }
	protected:
		node_iterator() : node_cache(nullptr), pos() {}
	private:
		NodeCache* node_cache;
		std::vector<std::pair<size_t, block_view<Node, NodeCache>>> pos;
	protected:
		bool is_empty() const { return pos.empty(); }
		bool is_root() const { return pos.size() == 1; }
		size_t node_index() const { return pos.back().first; }
		block_view<Node, NodeCache>& node() { return pos.back().second; }
		const Node& get() const { return pos.back().second.get(); }
	private:
		void parent() {
			pos.pop_back();
		}
		void child(size_t index) {
			pos.emplace_back(index, node_cache->read(child_ref(get(), index)));
		}
	protected:
		void next() {
			size_t level = pos.size() - 1;
			size_t index;
			for (;; level--) {
				if (level == 0) {
					throw std::invalid_argument("next doesn't exist");
				}
				index = pos[level].first + 1;
				if (index <= keys(pos[level - 1].second.get()).size()) {
					break;
				}
			}
			for (; level < pos.size(); level++, index = 0) {
				pos[level] = std::make_pair(index, node_cache->read(child_ref(pos[level - 1].second.get(), index)));
			}
		}
		void prev() {
			size_t level = pos.size() - 1;
			size_t index;
			for (;; level--) {
				if (level == 0) {
					throw std::invalid_argument("prev doesn't exist");
				}
				index = pos[level].first;
				if (index > 0) {
					break;
				}
			}
			for (index--; level < pos.size(); level++, index = keys(pos[level - 1].second.get()).size()) {
				pos[level] = std::make_pair(index, node_cache->read(child_ref(pos[level - 1].second.get(), index)));
			}
		}
	};

	class leaf_iterator : private node_iterator {
	private:
		friend class Tree;
	private:
		leaf_iterator(LeafCache& leaf_cache, block_ref root) : node_iterator(), leaf_cache(nullptr), leaf_index(0), leaf(leaf_cache.read(std::move(root))) {}
		leaf_iterator(node_iterator it, LeafCache& leaf_cache, size_t leaf_index) : node_iterator(std::move(it)), leaf_cache(&leaf_cache), leaf_index(leaf_index), leaf(leaf_cache.read(child_ref(node_iterator::get(), leaf_index))) {}
	private:
		LeafCache* leaf_cache;
		size_t leaf_index;
		block_view<Leaf, LeafCache> leaf;
	private:
		bool is_root() const { return node_iterator::is_empty(); }
	protected:
		bool operator==(const leaf_iterator& other) const { return leaf == other.leaf; }
		const Leaf& get() const { return leaf.get(); }
	protected:
		void next() {
			if (is_root()) {
				throw std::invalid_argument("next doesn't exist");
			}
			size_t index = leaf_index + 1;
			if (index > keys(node_iterator::get()).size()) {
				node_iterator::next();
				index = 0;
			}
			leaf_index = index;
			leaf = leaf_cache->read(child_ref(node_iterator::get(), leaf_index));
		}
		void prev() {
			if (is_root()) {
				throw std::invalid_argument("prev doesn't exist");
			}
			if (leaf_index == 0) {
				node_iterator::prev();
				leaf_index = keys(node_iterator::get()).size();
			} else {
				leaf_index--;
			}
			leaf = leaf_cache->read(child_ref(node_iterator::get(), leaf_index));
		}
	};

public:
	class iterator : private leaf_iterator {
	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = LeafEntry;
		using difference_type = std::ptrdiff_t;
		using pointer = const value_type*;
		using reference = const value_type&;
	private:
		friend class Tree;
	private:
		iterator(leaf_iterator it, size_t index) : leaf_iterator(std::move(it)), index(index) {}
	private:
		size_t index;
	private:
		bool is_end() const { return index >= leaf_iterator::get().size(); }
	private:
		void normalize() const {
			if (is_end()) {
				const_cast<iterator&>(*this).leaf_iterator::next();
				const_cast<iterator&>(*this).index = 0;
			}
		}
	public:
		bool operator==(const iterator& other) const {
			if (leaf_iterator::operator==(other)) {
				return index == other.index;
			}
			if (is_end()) {
				if (other.index == 0) {
					try {
						normalize();
						return *this == other;
					} catch (...) {
						return false;
					}
				} else {
					return false;
				}
			} else if (other.is_end()) {
				if (index == 0) {
					try {
						other.normalize();
						return *this == other;
					} catch (...) {
						return false;
					}
				} else {
					return false;
				}
			} else {
				return false;
			}
		}
		LeafEntry operator*() {
			normalize();
			return leaf_iterator::get()[index];
		}
		const LeafEntry* operator->() {
			normalize();
			return &leaf_iterator::get()[index];
		}
		iterator& operator++() {
			normalize();
			index++;
			return *this;
		}
		iterator& operator+=(size_t offset) {
			for (index += offset;;) {
				if (size_t size = leaf_iterator::get().size(); index > size) {
					leaf_iterator::next();
					index -= size;
				} else {
					break;
				}
			};
			return *this;
		}
		iterator& operator--() {
			if (index == 0) {
				leaf_iterator::prev();
				index = leaf_iterator::get().size();
				assert(index > 0);
			}
			index--;
			return *this;
		}
		iterator& operator-=(size_t offset) {
			for (;;) {
				if (index < offset) {
					offset -= index;
					leaf_iterator::prev();
					index = leaf_iterator::get().size();
					assert(index > 0);
				} else {
					index -= offset;
					break;
				}
			};
			return *this;
		}
	};

public:
	Tree(NodeCache& node_cache, LeafCache& leaf_cache, block_ref meta, Comp comp) : node_cache(node_cache), leaf_cache(leaf_cache), meta(BlockCacheLocal<Meta>::read(std::move(meta), [&] { return std::make_pair(leaf_cache.create().drop(), 0); })), comp(comp) {}

private:
	block_view_local<Meta> meta;
	Comp comp;

private:
	NodeCache& node_cache;
	LeafCache& leaf_cache;

private:
	size_t depth() const { return meta.get().second; }

private:
	node_iterator root_node() const {
		return node_iterator(node_cache, meta.get().first);
	}
	leaf_iterator root_leaf() const {
		return leaf_iterator(leaf_cache, meta.get().first);
	}

public:
	bool empty() const { return depth() == 0 && root_leaf().get().size() == 0; }

private:
	leaf_iterator find_leaf(auto f) const {
		size_t level = depth();
		if (level == 0) {
			return root_leaf();
		} else {
			for (node_iterator it = root_node();;) {
				size_t index = f(keys(it.get()));
				if (--level > 0) {
					it.child(index);
				} else {
					return leaf_iterator(std::move(it), leaf_cache, index);
				}
			}
		}
	}
	iterator find(leaf_iterator it, auto f) const {
		size_t index = f(it.get());
		return iterator(std::move(it), index);
	}

private:
	leaf_iterator leaf_front() const { return find_leaf([](const NodeKeys& keys) { return 0; }); }
	leaf_iterator leaf_back() const { return find_leaf([](const NodeKeys& keys) { return keys.size(); }); }

public:
	iterator begin() const { return find(leaf_front(), [](const Leaf& leaf) { return 0; }); }
	iterator end() const { return find(leaf_back(), [](const Leaf& leaf) { return leaf.size(); }); }

	std::reverse_iterator<iterator> rbegin() const { return std::reverse_iterator<iterator>(end()); }
	std::reverse_iterator<iterator> rend() const { return std::reverse_iterator<iterator>(begin()); }

private:
	template<class K>
	leaf_iterator lower_bound_leaf(const K& k) const {
		return find_leaf([&](const NodeKeys& keys) {
			return std::lower_bound(keys.begin(), keys.end(), k, [&](const NodeEntry& entry, const K& k) { return comp(key(entry), k); }) - keys.begin();
		});
	}
	template<class K>
	leaf_iterator upper_bound_leaf(const K& k) const {
		return find_leaf([&](const NodeKeys& keys) {
			return std::upper_bound(keys.begin(), keys.end(), k, [&](const K& k, const NodeEntry& entry) { return comp(k, key(entry)); }) - keys.begin();
		});
	}

public:
	template<class K>
	iterator lower_bound(const K& k) const {
		return find(lower_bound_leaf(k), [&](const Leaf& leaf) {
			return std::lower_bound(leaf.begin(), leaf.end(), k, [&](const LeafEntry& entry, const K& k) { return comp(key(entry), k); }) - leaf.begin();
		});
	}
	template<class K>
	iterator upper_bound(const K& k) const {
		return find(lower_bound_leaf(k), [&](const Leaf& leaf) {
			return std::upper_bound(leaf.begin(), leaf.end(), k, [&](const K& k, const LeafEntry& entry) { return comp(k, key(entry)); }) - leaf.begin();
		});
	}

public:
	void clear() {
		if (depth() == 0) {
			root_leaf().leaf.update([](Leaf& leaf) { leaf.clear(); });
		} else {
			meta.update([&](Meta& meta) { meta = std::make_pair(leaf_cache.create().drop(), 0); });
		}
	}

private:
	static bool node_should_split(const NodeKeys& keys) {
		return keys.size() > 2;
	}
	static bool leaf_should_split(const Leaf& leaf) {
		return leaf.size() > 3;
	}

private:
	static std::pair<Key, Node> split_node(NodeKeys& keys) {
		size_t index = keys.size() / 2;
		Key next_key = std::move(keys[index].first);
		Node next = std::make_pair(std::move(keys[index].second), NodeKeys(std::make_move_iterator(keys.begin() + index + 1), std::make_move_iterator(keys.end())));
		keys.erase(keys.begin() + index, keys.end());
		return std::make_pair(std::move(next_key), std::move(next));
	}
	static Leaf split_leaf(Leaf& leaf) {
		size_t index = leaf.size() / 2;
		Leaf next(std::make_move_iterator(leaf.begin() + index), std::make_move_iterator(leaf.end()));
		leaf.erase(leaf.begin() + index, leaf.end());
		return next;
	}

private:
	void insert_root(block_ref first, NodeEntry second) {
		meta.update([&](Meta& meta) {
			meta.first = node_cache.create(std::make_pair(std::move(first), NodeKeys{ std::move(second) })).drop();
			meta.second++;
		});
	}
	void insert_node_entry(node_iterator it, size_t index, NodeEntry entry) {
		it.node().update([&](Node& node) {
			auto& node_keys = keys(node);
			node_keys.emplace(node_keys.begin() + index, std::move(entry));
			if (node_should_split(node_keys)) {
				auto [next_key, next] = split_node(node_keys);
				insert_node_after(std::move(it), std::make_pair(std::move(next_key), node_cache.create(std::move(next)).drop()));
			}
		});
	}

private:
	void insert_node_after(node_iterator it, NodeEntry entry) {
		if (it.is_root()) {
			insert_root(it.node(), std::move(entry));
		} else {
			size_t node_index = it.node_index(); it.parent();
			insert_node_entry(std::move(it), node_index, std::move(entry));
		}
	}
	void insert_leaf_after(leaf_iterator it, NodeEntry entry) {
		if (it.is_root()) {
			insert_root(it.leaf, std::move(entry));
		} else {
			size_t leaf_index = it.leaf_index;
			insert_node_entry(std::move(it), leaf_index, std::move(entry));
		}
	}

public:
	void insert(iterator it, LeafEntry entry) {
		it.leaf.update([&](Leaf& leaf) {
			leaf.emplace(leaf.begin() + it.index, std::move(entry));
			if (leaf_should_split(leaf)) {
				Leaf next = split_leaf(leaf); Key next_key = key(next.front());
				insert_leaf_after(std::move(it), std::make_pair(std::move(next_key), leaf_cache.create(std::move(next)).drop()));
			}
		});
	}
	template<class K>
	void insert(const K& k, LeafEntry entry) {
		insert(upper_bound(k), std::move(entry));
	}

private:
	void erase_node_entry(node_iterator it, size_t index) {
		it.node().update([&](Node& node) {
			auto& node_keys = keys(node);
			if (index == 0) {
				if (node_keys.empty()) {
					erase_node(std::move(it));
				} else {
					node.first = std::move(node_keys.front().second);
					node_keys.erase(node_keys.begin());
				}
			} else {
				node_keys.erase(node_keys.begin() + index - 1);
			}
		});
	}

private:
	void erase_node(node_iterator it) {
		if (it.is_root()) {
			meta.update([&](Meta& meta) { meta = std::make_pair(leaf_cache.create().drop(), 0); });
		} else {
			size_t node_index = it.node_index(); it.parent();
			erase_node_entry(std::move(it), node_index);
		}
	}
	void erase_leaf(leaf_iterator it) {
		if (it.is_root()) {
		} else {
			size_t leaf_index = it.leaf_index;
			erase_node_entry(std::move(it), leaf_index);
		}
	}

public:
	void erase(iterator it) {
		it.normalize();
		it.leaf.update([&](Leaf& leaf) {
			leaf.erase(leaf.begin() + it.index);
			if (leaf.empty()) {
				erase_leaf(std::move(it));
			}
		});
	}
};


} // namespace BlockStore
