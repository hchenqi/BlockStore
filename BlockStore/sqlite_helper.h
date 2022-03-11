#pragma once

#include "uncopyable.h"

#include <string>
#include <vector>


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Sqlite)


class Query : private std::string, Uncopyable {
private:
	friend class Database;

public:
	using std::string::string;
	~Query();

private:
	ref_ptr<void> db = nullptr;
	alloc_ptr<void> command = nullptr;
	int para_num = 0;
};


struct BlobSize {
	size_t size;

	explicit BlobSize(size_t size) : size(size) {}
};


class Database : Uncopyable {
public:
	Database(const char file[]);
	~Database();

private:
	alloc_ptr<void> db;

private:
	void PrepareQuery(Query& query);
	void ExecuteQuery(Query& query);
private:
	void Bind(Query& query, uint64 value);
	void Bind(Query& query, BlobSize value);
public:
	template<class... ParaTypes>
	void Execute(Query& query, ParaTypes... para) {
		PrepareQuery(query);
		(Bind(query, para)...);
		ExecuteQuery(query);
	}

public:
	data_t GetLastInsertID();

public:
	std::vector<byte> ReadBlob(const char table[], const char column[], uint64 row);
	void WriteBlob(const char table[], const char column[], uint64 row, std::vector<byte> data);
};


END_NAMESPACE(Sqlite)

END_NAMESPACE(BlockStore)