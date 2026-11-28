#include "user/UserRepository.h"

namespace cramberry {
namespace {

constexpr const char* kUserColumns = "id, email, username, password_hash, status, created_at";

User rowToUser(const Statement& row) {
    User user;
    user.id = row.columnInt(0);
    user.email = row.columnText(1);
    user.username = row.columnText(2);
    user.passwordHash = row.columnText(3);
    user.status = row.columnText(4) == "deactivated" ? AccountStatus::Deactivated
                                                     : AccountStatus::Active;
    user.createdAt = row.columnInt(5);
    return user;
}

}  // namespace

SqliteUserRepository::SqliteUserRepository(const std::string& path) : db_(path) { migrate(); }

void SqliteUserRepository::migrate() {
    db_.exec(R"sql(
        CREATE TABLE IF NOT EXISTS users (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            email         TEXT NOT NULL UNIQUE,
            username      TEXT NOT NULL UNIQUE COLLATE NOCASE,
            password_hash TEXT NOT NULL,
            status        TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'deactivated')),
            created_at    INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS login_attempts (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            email      TEXT NOT NULL,
            success    INTEGER NOT NULL,
            ip         TEXT,
            created_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_attempts_email ON login_attempts (email, id);
        CREATE TABLE IF NOT EXISTS sessions (
            token_digest TEXT PRIMARY KEY,
            user_id      INTEGER NOT NULL REFERENCES users (id) ON DELETE CASCADE,
            expires_at   INTEGER NOT NULL
        );
    )sql");
}

CreateResult SqliteUserRepository::createUser(User& user) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        Statement st = db_.prepare(
            "INSERT INTO users (email, username, password_hash, status, created_at) "
            "VALUES (?1, ?2, ?3, 'active', ?4)");
        st.bind(1, user.email).bind(2, user.username).bind(3, user.passwordHash)
            .bind(4, user.createdAt);
        st.run();
        user.id = db_.lastInsertRowId();
        user.status = AccountStatus::Active;
        return CreateResult::Created;
    } catch (const DatabaseError& error) {
        if (!error.isConstraintViolation()) throw;
        const std::string message = error.what();  // e.g. "UNIQUE constraint failed: users.email"
        if (message.find("users.username") != std::string::npos) return CreateResult::UsernameTaken;
        if (message.find("users.email") != std::string::npos) return CreateResult::EmailTaken;
        throw;
    }
}

std::optional<User> SqliteUserRepository::findByEmail(const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare(std::string("SELECT ") + kUserColumns + " FROM users WHERE email = ?1");
    st.bind(1, email);
    if (!st.step()) return std::nullopt;
    return rowToUser(st);
}

std::optional<User> SqliteUserRepository::findById(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare(std::string("SELECT ") + kUserColumns + " FROM users WHERE id = ?1");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return rowToUser(st);
}

void SqliteUserRepository::setStatus(std::int64_t userId, AccountStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare("UPDATE users SET status = ?1 WHERE id = ?2");
    st.bind(1, std::string(status == AccountStatus::Active ? "active" : "deactivated")).bind(2, userId);
    st.run();
}

void SqliteUserRepository::recordLoginAttempt(const std::string& email, bool success,
                                              const std::string& ip, std::int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare(
        "INSERT INTO login_attempts (email, success, ip, created_at) VALUES (?1, ?2, ?3, ?4)");
    st.bind(1, email).bind(2, static_cast<std::int64_t>(success ? 1 : 0)).bind(3, ip).bind(4, now);
    st.run();
}

int SqliteUserRepository::countRecentFailures(const std::string& email, std::int64_t since) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare(
        "SELECT COUNT(*) FROM login_attempts "
        "WHERE email = ?1 AND success = 0 AND created_at >= ?2 "
        "AND id > COALESCE((SELECT MAX(id) FROM login_attempts WHERE email = ?1 AND success = 1), 0)");
    st.bind(1, email).bind(2, since);
    st.step();
    return static_cast<int>(st.columnInt(0));
}

void SqliteUserRepository::createSession(const std::string& tokenDigest, std::int64_t userId,
                                         std::int64_t expiresAt) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare(
        "INSERT INTO sessions (token_digest, user_id, expires_at) VALUES (?1, ?2, ?3)");
    st.bind(1, tokenDigest).bind(2, userId).bind(3, expiresAt);
    st.run();
}

std::optional<std::int64_t> SqliteUserRepository::findSessionUser(const std::string& tokenDigest,
                                                                  std::int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare(
        "SELECT user_id FROM sessions WHERE token_digest = ?1 AND expires_at > ?2");
    st.bind(1, tokenDigest).bind(2, now);
    if (!st.step()) return std::nullopt;
    return st.columnInt(0);
}

void SqliteUserRepository::deleteSession(const std::string& tokenDigest) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare("DELETE FROM sessions WHERE token_digest = ?1");
    st.bind(1, tokenDigest);
    st.run();
}

void SqliteUserRepository::deleteExpiredSessions(std::int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement st = db_.prepare("DELETE FROM sessions WHERE expires_at <= ?1");
    st.bind(1, now);
    st.run();
}

}  // namespace cramberry
