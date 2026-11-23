#include "user/UserRepository.h"

#include <array>

namespace cramberry {

namespace {

Student toStudent(const drogon::orm::Row& row) {
    Student s;
    s.id = row["id"].as<std::string>();
    s.name = row["name"].as<std::string>();
    s.email = row["email"].as<std::string>();
    s.utcOffsetMinutes = row["utc_offset_minutes"].as<int>();
    s.createdAt = row["created_at"].as<std::int64_t>();
    return s;
}

}  // namespace

std::optional<Student> SqlUserRepository::findById(const std::string& studentId) const {
    const auto rows = db_.exec(
        "SELECT id, name, email, utc_offset_minutes, created_at FROM students WHERE id = $1",
        studentId);
    if (rows.empty()) return std::nullopt;
    return toStudent(rows[0]);
}

std::optional<StoredCredentials> SqlUserRepository::findByEmail(const std::string& email) const {
    const auto rows = db_.exec(
        "SELECT id, name, email, utc_offset_minutes, created_at, password_hash FROM students "
        "WHERE email = $1",
        email);
    if (rows.empty()) return std::nullopt;
    return StoredCredentials{toStudent(rows[0]), rows[0]["password_hash"].as<std::string>()};
}

std::optional<std::string> SqlUserRepository::passwordHash(const std::string& studentId) const {
    const auto rows = db_.exec("SELECT password_hash FROM students WHERE id = $1", studentId);
    if (rows.empty()) return std::nullopt;
    return rows[0]["password_hash"].as<std::string>();
}

bool SqlUserRepository::create(const Student& student, const std::string& passwordHash) {
    if (findByEmail(student.email)) return false;
    try {
        db_.exec(
            "INSERT INTO students (id, name, email, password_hash, utc_offset_minutes, created_at) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            student.id, student.name, student.email, passwordHash, student.utcOffsetMinutes,
            static_cast<std::int64_t>(student.createdAt));
    } catch (...) {
        // Lost a race with another sign-up for the same email: report it as taken.
        if (findByEmail(student.email)) return false;
        throw;
    }
    return true;
}

void SqlUserRepository::update(const Student& student) {
    db_.exec("UPDATE students SET name = $1, utc_offset_minutes = $2 WHERE id = $3", student.name,
             student.utcOffsetMinutes, student.id);
}

void SqlUserRepository::remove(const std::string& studentId) {
    // Delete child rows explicitly rather than relying on ON DELETE CASCADE alone,
    // so a database without foreign-key enforcement still ends up clean (NFR-7).
    static constexpr std::array kOwnedTables = {
        "sessions",      "generation_runs", "notifications",         "study_blocks",
        "tasks",         "courses",         "notification_settings", "study_preferences",
    };
    auto tx = db_.begin();
    for (const char* table : kOwnedTables) {
        tx.exec(std::string("DELETE FROM ") + table + " WHERE student_id = $1", studentId);
    }
    tx.exec("DELETE FROM students WHERE id = $1", studentId);
    tx.commit();
}

void SqlUserRepository::createSession(const std::string& tokenHash, const std::string& studentId,
                                      Timestamp expiresAt) {
    db_.exec("INSERT INTO sessions (token_hash, student_id, expires_at) VALUES ($1, $2, $3)",
             tokenHash, studentId, static_cast<std::int64_t>(expiresAt));
}

std::optional<std::string> SqlUserRepository::findSession(const std::string& tokenHash,
                                                          Timestamp now) const {
    const auto rows = db_.exec(
        "SELECT student_id FROM sessions WHERE token_hash = $1 AND expires_at > $2", tokenHash,
        static_cast<std::int64_t>(now));
    if (rows.empty()) return std::nullopt;
    return rows[0]["student_id"].as<std::string>();
}

void SqlUserRepository::deleteSession(const std::string& tokenHash) {
    db_.exec("DELETE FROM sessions WHERE token_hash = $1", tokenHash);
}

}  // namespace cramberry
