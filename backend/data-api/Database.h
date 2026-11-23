#pragma once

// /data-api in the Development View: the only code that talks to the database.
// Production uses Supabase PostgreSQL; local development and the tests use SQLite.
// SQL is written once, PostgreSQL style ($1, $2, ...); Database rewrites the
// placeholders for SQLite, so every repository runs unchanged on both.

#include <drogon/orm/DbClient.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "data-api/Transaction.h"

namespace cramberry::data {

struct DatabaseConfig {
    // libpq connection string or URI, e.g. the Supabase "Session pooler" string.
    // When empty, SQLite is used instead.
    std::string postgresUrl;
    // SQLite file; ":memory:" gives a private, throwaway database (tests).
    std::string sqlitePath = "cramberry.db";
    std::size_t connections = 4;  // PostgreSQL only; SQLite always uses one connection
};

class Database {
public:
    enum class Kind { Sqlite, Postgres };

    // Connects and creates any missing tables. Throws if the database can't be reached.
    static std::shared_ptr<Database> open(const DatabaseConfig& config);
    static std::shared_ptr<Database> openInMemory();

    Kind kind() const { return kind_; }

    // Runs one statement and returns its rows. Throws on any database error.
    template <typename... Args>
    drogon::orm::Result exec(std::string_view sql, Args&&... args) const {
        return client_->execSqlSync(translate(sql), std::forward<Args>(args)...);
    }

    // Starts a transaction. Statements inside it must go through the Transaction,
    // not through exec(): with SQLite's single connection that would deadlock.
    Transaction begin() const;

    // "$1" -> "?1" for SQLite; unchanged for PostgreSQL.
    std::string translate(std::string_view sql) const;

    // True if a trivial query succeeds (used by GET /api/health).
    bool ping() const noexcept;

private:
    Database(Kind kind, drogon::orm::DbClientPtr client);
    void migrate() const;

    Kind kind_;
    drogon::orm::DbClientPtr client_;
};

}  // namespace cramberry::data
