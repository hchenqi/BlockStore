#include "sqlite_helper.h"
#include "../Sqlite3/sqlite3.h"

#include <stdexcept>


#pragma comment(lib, "sqlite3.lib")


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Sqlite)


static char* error_msg = nullptr;

struct Result {
	const Result& operator<<(int res) const {
		if (res != SQLITE_OK) { throw std::runtime_error("sqlite error"); }
		return *this;
	}
};

static constexpr Result res;


inline sqlite3* AsSqliteDb(void* db) { return static_cast<sqlite3*>(db); }
inline sqlite3** AsSqliteDb(void** db) { return reinterpret_cast<sqlite3**>(db); }

inline sqlite3_stmt* AsSqliteStmt(void* stmt) { return static_cast<sqlite3_stmt*>(stmt); }
inline sqlite3_stmt** AsSqliteStmt(void** stmt) { return reinterpret_cast<sqlite3_stmt**>(stmt); }


Query::~Query() {
	res << sqlite3_finalize(AsSqliteStmt(command));
}


Database::Database(const char file[]) : db(nullptr) {
	res << sqlite3_open(file, AsSqliteDb(&db));
}

Database::~Database() {
	res << sqlite3_close(AsSqliteDb(db));
}

void Database::PrepareQuery(Query& query) {
	if (query.command == nullptr) {
		query.db = db;
		res << sqlite3_prepare_v2(AsSqliteDb(db), query.c_str(), (int)query.length(), AsSqliteStmt(&query.command), nullptr);
	} else if (query.db != db) {
		throw std::invalid_argument("query database handle mismatch");
	}
	res << sqlite3_reset(AsSqliteStmt(query.command));
	query.para_num = 1;
}

void Database::ExecuteQuery(Query& query) {
	res << sqlite3_step(AsSqliteStmt(query.command));
}

void Database::Bind(Query& query, uint64 value) {
	res << sqlite3_bind_int64(AsSqliteStmt(query.command), query.para_num++, value);
}

void Database::Bind(Query& query, BlobSize value) {
	res << sqlite3_bind_zeroblob(AsSqliteStmt(query.command), query.para_num++, value.size);
}

data_t Database::GetLastInsertID() {
	return sqlite3_last_insert_rowid(AsSqliteDb(db));
}

std::vector<byte> Database::ReadBlob(const char table[], const char column[], uint64 row) {
	sqlite3_blob* blob;
	res << sqlite3_blob_open(AsSqliteDb(db), "main", table, column, row, false, &blob);
	std::vector<byte> data(sqlite3_blob_bytes(blob));
	res << sqlite3_blob_read(blob, data.data(), data.size(), 0);
	res << sqlite3_blob_close(blob);
	return data;
}

void Database::WriteBlob(const char table[], const char column[], uint64 row, std::vector<byte> data) {
	sqlite3_blob* blob;
	res << sqlite3_blob_open(AsSqliteDb(db), "main", table, column, row, true, &blob);
	res << sqlite3_blob_write(blob, data.data(), data.size(), 0);
	res << sqlite3_blob_close(blob);
}


END_NAMESPACE(Sqlite)

END_NAMESPACE(BlockStore)
