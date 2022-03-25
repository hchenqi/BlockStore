#pragma once

#include "uncopyable.h"
#include "layout.h"

#include <string>
#include <vector>
#include <span>


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Sqlite)


class Query : private std::string, Uncopyable {
public:
	using std::string::string;
	~Query();
private:
	friend class Database;
	alloc_ptr<void> command = nullptr;
};


class Database : Uncopyable {
public:
	Database(const char file[]);
	~Database();
private:
	alloc_ptr<void> db;
private:
	void PrepareQuery(Query& query);
	bool ExecuteQuery(Query& query);
private:
	void Bind(Query& query, uint64 value);
	void Bind(Query& query, const std::string& value);
	void Bind(Query& query, std::pair<const byte*, size_t> value);
	template<class T>
	void Bind(Query& query, std::span<const T> value) {
		Bind(query, { reinterpret_cast<const byte*>(value.data()), value.size_bytes() });
	}
	template<class T>
	void Bind(Query& query, const std::vector<T>& value) {
		Bind(query, std::span<const T>(value));
	}
private:
	void Read(Query& query, uint64& value);
	void Read(Query& query, std::string& value);
	void Read(Query& query, std::pair<const byte*, size_t>& value);
	void Read(Query& query, bool& value) { uint64 temp; Read(query, temp); value = static_cast<bool>(temp); }
	template<class T>
	void Read(Query& query, std::span<const T>& value) {
		std::pair<const byte*, size_t> value_pair;
		Read(query, value_pair);
		if (value_pair.second % sizeof(T) != 0) { throw std::runtime_error("blob length error"); }
		value = std::span<const T>(reinterpret_cast<const T*>(value_pair.first), value_pair.second / sizeof(T));
	}
	template<class T>
	void Read(Query& query, std::vector<T>& value) {
		std::span<const T> value_span;
		Read(query, value_span);
		value.assign(value_span.begin(), value_span.end());
	}
	template<class T1, class T2>
	void Read(Query& query, std::pair<T1, T2>& value) {
		Read(query, value.first); Read(query, value.second);
	}
	template<class... Ts>
	void Read(Query& query, std::tuple<Ts...>& value) {
		std::apply([&](auto&... member) { (Read(query, member), ...); }, value);
	}
public:
	template<class... Ts>
	void Execute(Query& query, const Ts&... para) {
		PrepareQuery(query);
		(Bind(query, para), ...);
		ExecuteQuery(query);
	}
	template<class T, class... Ts>
	T ExecuteForOne(Query& query, const Ts&... para) {
		PrepareQuery(query); (Bind(query, para), ...);
		if (ExecuteQuery(query) == false) { throw std::runtime_error("sqlite error"); }
		T result; Read(query, result); return result;
	}
	template<class T, class... Ts>
	std::vector<T> ExecuteForMultiple(Query& query, const Ts&... para) {
		PrepareQuery(query); (Bind(query, para), ...);
		std::vector<T> result; while (ExecuteQuery(query)) { Read(query, result.emplace_back()); } return result;
	}
};


END_NAMESPACE(Sqlite)

END_NAMESPACE(BlockStore)