#pragma once

#include "../data/cache.h"
#include "CppSerialize/stl/optional.h"
#include "CppSerialize/stl/vector.h"

#include <cassert>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <vector>


namespace BlockStore {


template<class Key, class Value, class NodeCache, class LeafCache, class Comp>
class Tree {
private:
	using Meta = std::pair<block_ref, size_t>;
	using NodeEntry = std::pair<Key, block_ref>;
	using Node = std::vector<NodeEntry>;
	using LeafEntry = std::conditional_t<std::is_void_v<Value>, Key, std::pair<Key, Value>>;
	using Leaf = std::vector<LeafEntry>;

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
			pos.emplace_back(index, cache->read(get()[index].second));
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
				pos[level] = std::make_pair(index, cache->read(pos[level - 1].second.get()[index].second));
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
				pos[level] = std::make_pair(index - 1, cache->read(pos[level - 1].second.get()[index - 1].second));
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
		bool is_end() const { return index >= get().size(); }
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
			return get()[index];
		}
		const LeafEntry* operator->() {
			normalize();
			return &get()[index];
		}
		iterator& operator++() {
			normalize();
			index++;
			return *this;
		}
		iterator& operator+=(size_t offset) {
			for (index += offset;;) {
				if (size_t size = get().size(); index > size) {
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
				index = get().size();
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
					index = get().size();
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
	Tree(NodeCache& node_cache, LeafCache& leaf_cache, block_ref ref) : node_cache(node_cache), leaf_cache(leaf_cache), meta(BlockCacheLocal<Meta>::read(std::move(ref), [&] { return std::make_pair(leaf_cache.create(), 0); })) {}

private:
	block_view_local<Meta> meta;

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

	leaf_iterator leaf_front() const {
		size_t level = depth();
		if (level == 0) {
			return root_leaf();
		} else {
			for (node_iterator it = root_node();;) {
				size_t index = 0;
				if (--level > 0) {
					it.child(index);
				} else {
					return leaf_iterator(std::move(it), leaf_cache, index);
				}
			}
		}
	}

	leaf_iterator leaf_back() const {
		size_t level = depth();
		if (level == 0) {
			return root_leaf();
		} else {
			for (node_iterator it = root_node();;) {
				size_t index = it.get().size() - 1;
				if (--level > 0) {
					it.child(index);
				} else {
					return leaf_iterator(std::move(it), leaf_cache, index);
				}
			}
		}
	}

public:
	iterator begin() const {
		return iterator(leaf_front(), 0);
	}

	iterator end() const {
		leaf_iterator it = leaf_back();
		size_t index = it.get().size() - 1;
		return iterator(std::move(it), index);
	}

private:
	leaf_iterator find_leaf(auto f) {

	}

private:
	template<class K>
	leaf_iterator lower_bound_leaf(const K& k) const {
		size_t level = depth();
		if (level == 0) {
			return root_leaf();
		} else {
			for (node_iterator it = root_node();;) {
				const Node& node = it.get();
				assert(node.size() >= 2);
				size_t index = std::lower_bound(node.begin(), node.end(), k, [&](const NodeEntry& entry, K& k) { return Comp()(key(entry), k); }) - node.begin();
				if (index > 0) { index--; }
				if (--level > 0) {
					it.child(index);
				} else {
					return leaf_iterator(std::move(it), leaf_cache, index);
				}
			}
		}
	}

	template<class K>
	leaf_iterator upper_bound_leaf(const K& k) const {
		size_t level = depth();
		if (level == 0) {
			return root_leaf();
		} else {
			node_iterator it = root_node();
			while (level--) {
				const Node& node = it.get();
				assert(node.size() >= 2);
				size_t index = std::upper_bound(node.begin(), node.end(), k, [&](const NodeEntry& entry, K& k) { return Comp()(key(entry), k); }) - node.begin();
				if (index > 0) { index--; }
				if (level > 0) {
					it.child(index);
				} else {
					return leaf_iterator(std::move(it), leaf_cache, index);
				}
			}
		}
	}

private:
	void insert_at(iterator it, LeafEntry entry) {}

public:
	template<class K>
	iterator lower_bound(const K& k) const {
		leaf_iterator it = lower_bound_leaf(k);
		const Leaf& leaf = it.get();
		assert(leaf.size() >= 2);
		size_t index = std::lower_bound(leaf.begin(), leaf.end(), k, [&](const LeafEntry& entry, K& k) { return Comp()(key(entry), k); }) - node.begin();
		return iterator(it, index);
	}

	template<class K>
	iterator upper_bound(const K& k) const {
		leaf_iterator it = upper_bound_leaf(k);
		const Leaf& leaf = it.get();
		assert(leaf.size() >= 2);
		size_t index = std::upper_bound(leaf.begin(), leaf.end(), k, [&](const LeafEntry& entry, K& k) { return Comp()(key(entry), k); }) - node.begin();
		return iterator(it, index);
	}

	void insert(LeafEntry entry) {
		auto [found, it] = find(key(entry));
		if (found) {
			throw std::invalid_argument("index")
		}

	}
};


} // namespace BlockStore
