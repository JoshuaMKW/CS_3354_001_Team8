#include "data-api/Database.h"

#include <filesystem>
#include <fstream>

#include <array>
#include <cctype>
#include <stdexcept>

namespace cramberry::data {

namespace {

// Portable DDL: runs on both SQLite and PostgreSQL. Booleans are INTEGER 0/1,
// times are BIGINT minutes since the Unix epoch (UTC), lists are JSON text.
// Every student-owned table is keyed by student_id so each query can be scoped
// to the signed-in student (NFR-19).
constexpr std::array kSchema = {
    R"(CREATE TABLE IF NOT EXISTS students (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        email TEXT NOT NULL UNIQUE,
        password_hash TEXT NOT NULL,
        utc_offset_minutes INTEGER NOT NULL DEFAULT 0,
        created_at BIGINT NOT NULL))",
    R"(CREATE TABLE IF NOT EXISTS sessions (
        token_hash TEXT PRIMARY KEY,
        student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        expires_at BIGINT NOT NULL))",
    R"(CREATE TABLE IF NOT EXISTS courses (
        id TEXT PRIMARY KEY,
        student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        code TEXT NOT NULL DEFAULT '',
        term TEXT NOT NULL DEFAULT ''))",
    R"(CREATE TABLE IF NOT EXISTS tasks (
        id TEXT PRIMARY KEY,
        student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        course_id TEXT NOT NULL DEFAULT '',
        title TEXT NOT NULL,
        kind TEXT NOT NULL,
        due_at BIGINT NOT NULL,
        estimated_minutes INTEGER NOT NULL,
        priority INTEGER NOT NULL,
        status TEXT NOT NULL,
        completed_at BIGINT NOT NULL DEFAULT 0,
        notes TEXT NOT NULL DEFAULT '',
        created_at BIGINT NOT NULL))",
    R"(CREATE INDEX IF NOT EXISTS tasks_student_due ON tasks (student_id, due_at))",
    R"(CREATE TABLE IF NOT EXISTS study_preferences (
        student_id TEXT PRIMARY KEY REFERENCES students(id) ON DELETE CASCADE,
        preferences TEXT NOT NULL,
        updated_at BIGINT NOT NULL))",
    R"(CREATE TABLE IF NOT EXISTS notification_settings (
        student_id TEXT PRIMARY KEY REFERENCES students(id) ON DELETE CASCADE,
        enabled INTEGER NOT NULL,
        email INTEGER NOT NULL,
        desktop INTEGER NOT NULL,
        sms INTEGER NOT NULL,
        phone_number TEXT NOT NULL,
        deadline_reminders INTEGER NOT NULL,
        deadline_lead_minutes INTEGER NOT NULL,
        updated_at BIGINT NOT NULL))",
    R"(CREATE TABLE IF NOT EXISTS study_blocks (
        id TEXT PRIMARY KEY,
        student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        assignment_id TEXT NOT NULL,
        title TEXT NOT NULL,
        day INTEGER NOT NULL,
        start_minute INTEGER NOT NULL,
        end_minute INTEGER NOT NULL,
        sort_order INTEGER NOT NULL))",
    R"(CREATE INDEX IF NOT EXISTS study_blocks_student ON study_blocks (student_id))",
    R"(CREATE TABLE IF NOT EXISTS schedule_shares (
        id TEXT PRIMARY KEY,
        owner_student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        recipient_student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        created_at BIGINT NOT NULL,
        UNIQUE (owner_student_id, recipient_student_id),
        CHECK (owner_student_id <> recipient_student_id)))",
    R"(CREATE INDEX IF NOT EXISTS schedule_shares_recipient ON schedule_shares (recipient_student_id))",
    R"(CREATE TABLE IF NOT EXISTS notifications (
        id TEXT PRIMARY KEY,
        student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        channel TEXT NOT NULL,
        subject TEXT NOT NULL,
        body TEXT NOT NULL,
        sent_at BIGINT NOT NULL,
        is_read INTEGER NOT NULL DEFAULT 0))",
    R"(CREATE INDEX IF NOT EXISTS notifications_student ON notifications (student_id, sent_at))",
    R"(CREATE TABLE IF NOT EXISTS generation_runs (
        id TEXT PRIMARY KEY,
        student_id TEXT NOT NULL REFERENCES students(id) ON DELETE CASCADE,
        created_at BIGINT NOT NULL,
        engine TEXT NOT NULL,
        ok INTEGER NOT NULL,
        block_count INTEGER NOT NULL,
        details TEXT NOT NULL))",
};

// Drogon's connection-string parser treats '\' as an escape and splits on spaces, so
// Windows paths like C:\Users\Me\cramberry.db must be quoted and escaped.
std::string quoteConnValue(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\\' || c == '\'') out += '\\';
        out += c;
    }
    return out + "'";
}

}  // namespace

Database::Database(Kind kind, drogon::orm::DbClientPtr client)
    : kind_(kind), client_(std::move(client)) {}

std::shared_ptr<Database> Database::open(const DatabaseConfig& config) {
    std::shared_ptr<Database> db;
    if (!config.postgresUrl.empty()) {
        auto client = drogon::orm::DbClient::newPgClient(config.postgresUrl, config.connections);
        if (!client) throw std::runtime_error("this build has no PostgreSQL support (vcpkg feature 'supabase')");
        db.reset(new Database(Kind::Postgres, std::move(client)));
    } else {
        // Drogon retries a SQLite file it can't open once a second forever (and asserts
        // in Debug builds), so check the path first and fail with a clear message.
        // (A plain file open, not sqlite3_open: that would initialise SQLite before
        // Drogon configures it.)
        if (config.sqlitePath != ":memory:" &&
            !std::ofstream(std::filesystem::u8path(config.sqlitePath), std::ios::binary | std::ios::app)) {
            throw std::runtime_error("can't open SQLite database '" + config.sqlitePath +
                                     "': check that the folder exists and is writable");
        }
        // One connection: SQLite serialises writes anyway, and ":memory:" databases
        // are private to the connection that created them.
        auto client = drogon::orm::DbClient::newSqlite3Client("filename=" + quoteConnValue(config.sqlitePath), 1);
        if (!client) throw std::runtime_error("this build has no SQLite support");
        db.reset(new Database(Kind::Sqlite, std::move(client)));
    }
    db->client_->setTimeout(10.0);
    if (db->kind_ == Kind::Sqlite) {
        db->exec("PRAGMA foreign_keys = ON");
    }
    db->migrate();
    return db;
}

std::shared_ptr<Database> Database::openInMemory() {
    DatabaseConfig config;
    config.sqlitePath = ":memory:";
    return open(config);
}

void Database::migrate() const {
    for (const char* statement : kSchema) {
        exec(statement);
    }
}

Transaction Database::begin() const {
    return Transaction(client_->newTransaction(),
                       [this](std::string_view sql) { return translate(sql); });
}

std::string Database::translate(std::string_view sql) const {
    if (kind_ == Kind::Postgres) return std::string(sql);
    std::string out;
    out.reserve(sql.size());
    for (std::size_t i = 0; i < sql.size(); ++i) {
        const bool placeholder = sql[i] == '$' && i + 1 < sql.size() &&
                                 std::isdigit(static_cast<unsigned char>(sql[i + 1]));
        out += placeholder ? '?' : sql[i];
    }
    return out;
}

bool Database::ping() const noexcept {
    try {
        exec("SELECT 1");
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace cramberry::data
