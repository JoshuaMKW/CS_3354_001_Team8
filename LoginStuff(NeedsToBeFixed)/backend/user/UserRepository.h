#pragma once
// Storage interface. AuthService only talks to this, so SQLite can later be swapped
// for a Supabase/PostgREST implementation without touching the login logic.
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "data-api/Database.h"
#include "user/User.h"

namespace cramberry {

enum class CreateResult { Created, EmailTaken, UsernameTaken };

class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    virtual CreateResult createUser(User& user) = 0;  // fills user.id when Created
    virtual std::optional<User> findByEmail(const std::string& email) = 0;
    virtual std::optional<User> findById(std::int64_t id) = 0;
    virtual void setStatus(std::int64_t userId, AccountStatus status) = 0;

    virtual void recordLoginAttempt(const std::string& email, bool success,
                                    const std::string& ip, std::int64_t now) = 0;
    // Failed attempts for this email since `since`, ignoring any before its last successful login.
    virtual int countRecentFailures(const std::string& email, std::int64_t since) = 0;

    virtual void createSession(const std::string& tokenDigest, std::int64_t userId,
                               std::int64_t expiresAt) = 0;
    virtual std::optional<std::int64_t> findSessionUser(const std::string& tokenDigest,
                                                        std::int64_t now) = 0;
    virtual void deleteSession(const std::string& tokenDigest) = 0;
    virtual void deleteExpiredSessions(std::int64_t now) = 0;
};

class SqliteUserRepository : public IUserRepository {
public:
    explicit SqliteUserRepository(const std::string& path);  // creates tables if missing

    CreateResult createUser(User& user) override;
    std::optional<User> findByEmail(const std::string& email) override;
    std::optional<User> findById(std::int64_t id) override;
    void setStatus(std::int64_t userId, AccountStatus status) override;
    void recordLoginAttempt(const std::string& email, bool success, const std::string& ip,
                            std::int64_t now) override;
    int countRecentFailures(const std::string& email, std::int64_t since) override;
    void createSession(const std::string& tokenDigest, std::int64_t userId,
                       std::int64_t expiresAt) override;
    std::optional<std::int64_t> findSessionUser(const std::string& tokenDigest,
                                                std::int64_t now) override;
    void deleteSession(const std::string& tokenDigest) override;
    void deleteExpiredSessions(std::int64_t now) override;

private:
    void migrate();
    Database db_;
    std::mutex mutex_;  // the HTTP server is multi-threaded; one connection, one lock
};

}  // namespace cramberry
