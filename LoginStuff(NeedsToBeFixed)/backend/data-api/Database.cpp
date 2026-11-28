#include "data-api/Database.h"

#include <sqlite3.h>

#include <utility>

namespace cramberry {

DatabaseError::DatabaseError(int code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

bool DatabaseError::isConstraintViolation() const {
    return (code_ & 0xFF) == SQLITE_CONSTRAINT;  // mask handles extended result codes
}

// ---- Statement ----
Statement::Statement(sqlite3* db, const std::string& sql) {
    const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) throw DatabaseError(rc, sqlite3_errmsg(db));
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::bind(int index, const std::string& value) {
    const int rc = sqlite3_bind_text(stmt_, index, value.c_str(),
                                     static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) throw DatabaseError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK) throw DatabaseError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DatabaseError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
}

std::string Statement::columnText(int column) const {
    const unsigned char* text = sqlite3_column_text(stmt_, column);
    if (!text) return {};
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)));
}

std::int64_t Statement::columnInt(int column) const {
    return sqlite3_column_int64(stmt_, column);
}

// ---- Database ----
Database::Database(const std::string& path) {
    const int rc = sqlite3_open_v2(
        path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "out of memory";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError(rc, "Could not open database '" + path + "': " + message);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys = ON;");
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec(const std::string& sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        const std::string message = error ? error : "unknown SQLite error";
        sqlite3_free(error);
        throw DatabaseError(rc, message);
    }
}

Statement Database::prepare(const std::string& sql) { return Statement(db_, sql); }

std::int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

// ---- Transaction ----
Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN;"); }

Transaction::~Transaction() {
    if (!done_) {
        try { db_.exec("ROLLBACK;"); } catch (...) {}
    }
}

void Transaction::commit() {
    db_.exec("COMMIT;");
    done_ = true;
}

}  // namespace cramberry
