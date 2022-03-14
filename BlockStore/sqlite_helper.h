#pragma once

#include "uncopyable.h"

#include <string>
#include <vector>
#include <optional>


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
	void Bind(Query& query, const std::vector<byte>& value);
private:
	void Read(Query& query, uint64& value);
	void Read(Query& query, std::string& value);
	void Read(Query& query, std::vector<byte>& value);
	void Read(Query& query, bool& value) { uint64 temp; Read(query, temp); value = static_cast<bool>(temp); }
	template<class... Ts>
	void Read(Query& query, std::tuple<Ts...>& value) {
		std::apply([&](auto&... member) { (Read(query, member), ...); }, value);
	}
public:
	template<class... Ts>
	void Execute(Query& query, Ts... para) {
		PrepareQuery(query);
		(Bind(query, para), ...);
		ExecuteQuery(query);
	}
	template<class T, class... Ts>
	T ExecuteForOne(Query& query, Ts... para) {
		PrepareQuery(query); (Bind(query, para), ...);
		if (ExecuteQuery(query) == false) { throw std::runtime_error("sqlite error"); }
		T result; Read(query, result); return result;
	}
	template<class T, class... Ts>
	std::optional<T> ExecuteForOneOrNone(Query& query, Ts... para) {
		PrepareQuery(query); (Bind(query, para), ...);
		if (ExecuteQuery(query) == false) { return {}; }
		T result; Read(query, result); return result;
	}
	template<class T, class... Ts>
	std::vector<T> ExecuteForMultiple(Query& query, Ts... para) {
		PrepareQuery(query); (Bind(query, para), ...);
		std::vector<T> result; while (ExecuteQuery(query)) { Read(query, result.emplace_back()); } return result;
	}

public:
	data_t GetLastInsertID();
};


END_NAMESPACE(Sqlite)

END_NAMESPACE(BlockStore)