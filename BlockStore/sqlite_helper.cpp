#include "sqlite_helper.h"
#include "../Sqlite3/sqlite3.h"


#pragma comment(lib, "sqlite3.lib")


BEGIN_NAMESPACE(BlockStore)

BEGIN_NAMESPACE(Sqlite)

BEGIN_NAMESPACE(Anonymous)

static constexpr size_t max_blob_length = 0x1000000;


static int index = 0;

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


END_NAMESPACE(Anonymous)


Query::~Query() {
	res << sqlite3_finalize(AsSqliteStmt(command));
}


Database::Database(const char file[]) : db(nullptr) {
	res << sqlite3_open(file, AsSqliteDb(&db));
}

Database::~Database() {
	res << sqlite3_close_v2(AsSqliteDb(db));
}

void Database::PrepareQuery(Query& query) {
	if (query.command == nullptr) {
		res << sqlite3_prepare_v2(AsSqliteDb(db), query.c_str(), (int)query.length(), AsSqliteStmt(&query.command), nullptr);
	}
	res << sqlite3_reset(AsSqliteStmt(query.command));
	index = 1;
}

bool Database::ExecuteQuery(Query& query) {
	int ret = sqlite3_step(AsSqliteStmt(query.command));
	index = 0;
	if (ret == SQLITE_ROW) { return true; }
	if (ret == SQLITE_DONE) { return false; }
	throw std::runtime_error("sqlite error");
}

void Database::Bind(Query& query, uint64 value) {
	res << sqlite3_bind_int64(AsSqliteStmt(query.command), index++, value);
}

void Database::Bind(Query& query, const std::string& value) {
	if (value.size() > max_blob_length) { throw std::invalid_argument("string too long"); }
	res << sqlite3_bind_text(AsSqliteStmt(query.command), index++, value.data(), (int)value.size(), SQLITE_TRANSIENT);
}

void Database::Bind(Query& query, std::pair<const byte*, size_t> value) {
	if (value.second > max_blob_length) { throw std::invalid_argument("blob size too large"); }
	res << sqlite3_bind_blob(AsSqliteStmt(query.command), index++, value.first, (int)value.second, SQLITE_TRANSIENT);
}

void Database::Read(Query& query, uint64& value) {
	value = sqlite3_column_int64(AsSqliteStmt(query.command), index++);
}

void Database::Read(Query& query, std::string& value) {
	const char* data = (const char*)sqlite3_column_text(AsSqliteStmt(query.command), index);
	size_t size = sqlite3_column_bytes(AsSqliteStmt(query.command), index++);
	value.assign(data, data + size);
}

void Database::Read(Query& query, std::pair<const byte*, size_t>& value) {
	value.first = static_cast<const byte*>(sqlite3_column_blob(AsSqliteStmt(query.command), index));
	value.second = sqlite3_column_bytes(AsSqliteStmt(query.command), index++);
}


END_NAMESPACE(Sqlite)

END_NAMESPACE(BlockStore)