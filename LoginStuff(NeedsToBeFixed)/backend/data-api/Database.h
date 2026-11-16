#pragma once
// Thin RAII wrapper over SQLite. The rest of the app never touches sqlite3 directly.
#include <cstdint>
#include <stdexcept>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace cramberry {

class DatabaseError : public std::runtime_error {
public:
    DatabaseError(int code, const std::string& message);
    int code() const { return code_; }
    bool isConstraintViolation() const;  // UNIQUE, NOT NULL, CHECK, FOREIGN KEY...
private:
    int code_;
};

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement& operator=(Statement&&) = delete;

    // Parameters are 1-based. Always bind values; never build SQL by concatenation.
    Statement& bind(int index, const std::string& value);
    Statement& bind(int index, std::int64_t value);

    bool step();  // true = a row is ready, false = finished
    void run() { step(); }

    std::string columnText(int column) const;
    std::int64_t columnInt(int column) const;

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    explicit Database(const std::string& path);  // ":memory:" works for tests
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const std::string& sql);
    Statement prepare(const std::string& sql);
    std::int64_t lastInsertRowId() const;

private:
    sqlite3* db_ = nullptr;
};

// BEGIN ... COMMIT, rolled back automatically if commit() is never reached.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Database& db_;
    bool done_ = false;
};

}  // namespace cramberry
