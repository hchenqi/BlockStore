#pragma once

#include "uncopyable.h"

#include <string>


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
};


class Database : Uncopyable {
public:
	Database(const char file[]);
	~Database();
private:
	alloc_ptr<void> db;
public:
	void Execute(Query& query);
public:
	data_t GetLastInsertID();
};


END_NAMESPACE(Sqlite)

END_NAMESPACE(BlockStore)