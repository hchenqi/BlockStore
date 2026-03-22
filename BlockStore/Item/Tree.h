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
using TreeNode = std::vector<TreeNodeEntry<Key>>;

template<class Key, class Value>
using TreeLeafEntry = std::conditional_t<std::is_void_v<Value>, Key, std::pair<Key, Value>>;

template<class Key, class Value>
using TreeLeaf = std::vector<TreeLeafEntry<Key, Value>>;


template<class Key, class Value, class Comp, template<class T> class Cache>
class Tree {
private:
	using Meta = std::pair<block_ref, size_t>;
	using NodeEntry = TreeNodeEntry<Key>;
	using Node = TreeNode<Key>;
	using LeafEntry = TreeLeafEntry<Key, Value>;
	using Leaf = TreeLeaf<Key, Value>;
protected:
	using NodeCache = Cache<Node>;
	using LeafCache = Cache<Leaf>;

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
		const Node& get() const { return pos.back().second.get(); }
	private:
		void parent() {
			pos.pop_back();
		}
		void child(size_t index) {
			pos.emplace_back(index, node_cache->read(get()[index].second));
		}
	protected:
		void next() {
			size_t level = pos.size();
			size_t index;
			for (;; level--) {
				if (level == 0) {
					throw std::invalid_argument("next doesn't exist");
				}
				index = pos[level].first + 1;
				if (index < pos[level - 1].second.get().size()) {
					break;
				}
			}
			for (; level < pos.size(); level++, index = 0) {
				pos[level] = std::make_pair(index, node_cache->read(pos[level - 1].second.get()[index].second));
			}
		}
		void prev() {
			size_t level = pos.size();
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
			for (; level < pos.size(); level++, index = pos[level - 1].second.get().size()) {
				pos[level] = std::make_pair(index - 1, node_cache->read(pos[level - 1].second.get()[index - 1].second));
			}
		}
	};

	class leaf_iterator : private node_iterator {
	private:
		friend class Tree;
	private:
		leaf_iterator(LeafCache& leaf_cache, block_ref root) : node_iterator(), leaf_cache(nullptr), leaf_index(0), leaf(leaf_cache.read(std::move(root))) {}
		leaf_iterator(node_iterator it, LeafCache& leaf_cache, size_t leaf_index) : node_iterator(std::move(it)), leaf_cache(&leaf_cache), leaf_index(leaf_index), leaf(leaf_cache.read(node_iterator::get()[leaf_index].second)) {}
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
			if (index >= node_iterator::get().size()) {
				node_iterator::next();
				index = 0;
			}
			leaf_index = index;
			leaf = leaf_cache->read(node_iterator::get()[leaf_index].second);
		}
		leaf_iterator& prev() {
			if (is_root()) {
				throw std::invalid_argument("prev doesn't exist");
			}
			if (leaf_index == 0) {
				node_iterator::prev();
				leaf_index = node_iterator::get().size();
			}
			leaf_index--;
			leaf = leaf_cache->read(node_iterator::get()[leaf_index].second);
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
			if (leaf_iterator::operator==(other) && index == other.index) {
				return true;
			}
			if (is_end()) {
				try {
					normalize();
					return *this == other;
				} catch (...) {
					return false;
				}
			} else if (other.is_end()) {
				try {
					other.normalize();
					return *this == other;
				} catch (...) {
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
	Tree(NodeCache& node_cache, LeafCache& leaf_cache, block_ref meta, Comp comp) : node_cache(node_cache), leaf_cache(leaf_cache), meta(BlockCacheLocal<Meta>::read(std::move(meta), [&] { return std::make_pair(leaf_cache.create(), 0); })), comp(comp) {}

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

private:
	leaf_iterator find_leaf(auto f) const {
		size_t level = depth();
		if (level == 0) {
			return root_leaf();
		} else {
			for (node_iterator it = root_node();;) {
				size_t index = f(it);
				if (--level > 0) {
					it.child(index);
				} else {
					return leaf_iterator(std::move(it), leaf_cache, index);
				}
			}
		}
	}
	iterator find(leaf_iterator it, auto f) const {
		size_t index = f(it);
		return iterator(std::move(it), index);
	}

private:
	leaf_iterator leaf_front() const { return find_leaf([](const node_iterator& it) { return 0; }); }
	leaf_iterator leaf_back() const { return find_leaf([](const node_iterator& it) { return it.get().size() - 1; }); }

public:
	iterator begin() const { return find(leaf_front(), [](const leaf_iterator& it) { return 0; }); }
	iterator end() const { return find(leaf_back(), [](const leaf_iterator& it) { return it.get().size(); }); }

private:
	template<class K>
	leaf_iterator lower_bound_leaf(const K& k) const {
		return find_leaf([&](const node_iterator& it) {
			const Node& node = it.get();
			size_t index = std::lower_bound(node.begin(), node.end(), k, [&](const NodeEntry& entry, const K& k) { return comp(key(entry), k); }) - node.begin();
			return index > 0 ? index - 1 : index;
		});
	}
	template<class K>
	leaf_iterator upper_bound_leaf(const K& k) const {
		return find_leaf([&](const node_iterator& it) {
			const Node& node = it.get();
			size_t index = std::upper_bound(node.begin(), node.end(), k, [&](const K& k, const NodeEntry& entry) { return comp(k, key(entry)); }) - node.begin();
			return index > 0 ? index - 1 : index;
		});
	}

public:
	template<class K>
	iterator lower_bound(const K& k) const {
		return find(lower_bound_leaf(k), [&](const leaf_iterator& it) {
			const Leaf& leaf = it.get();
			return std::lower_bound(leaf.begin(), leaf.end(), k, [&](const LeafEntry& entry, const K& k) { return comp(key(entry), k); }) - leaf.begin();
		});
	}
	template<class K>
	iterator upper_bound(const K& k) const {
		return find(lower_bound_leaf(k), [&](const leaf_iterator& it) {
			const Leaf& leaf = it.get();
			return std::upper_bound(leaf.begin(), leaf.end(), k, [&](const K& k, const LeafEntry& entry) { return comp(k, key(entry)); }) - leaf.begin();
		});
	}

private:
	template<class K>
	void insert_at(iterator it, const K& k, LeafEntry entry) {

	}

public:
	template<class K>
	void insert(const K& k, LeafEntry entry) {
		insert_at(upper_bound(k), k, std::move(entry));
	}
};


} // namespace BlockStore
