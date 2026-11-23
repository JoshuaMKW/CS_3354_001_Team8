#pragma once

#include <optional>
#include <string>

#include "common/Time.h"

namespace cramberry {

// StudentAccount in the UML class diagram.
struct Student {
    std::string id;
    std::string name;
    std::string email;              // stored lower-case; unique
    int utcOffsetMinutes = 0;       // e.g. -360 for US Central Standard Time
    Timestamp createdAt = 0;
};

struct StoredCredentials {
    Student student;
    std::string passwordHash;
};

// Accounts and sign-in sessions (Supabase in production, SQLite locally).
class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    virtual std::optional<Student> findById(const std::string& studentId) const = 0;
    virtual std::optional<StoredCredentials> findByEmail(const std::string& email) const = 0;
    virtual std::optional<std::string> passwordHash(const std::string& studentId) const = 0;
    // Returns false if the email is already registered.
    virtual bool create(const Student& student, const std::string& passwordHash) = 0;
    virtual void update(const Student& student) = 0;
    // FR-14: removes the account and everything that belongs to it, all or nothing.
    virtual void remove(const std::string& studentId) = 0;

    // Sessions are stored by the SHA-256 of the token, never the token itself.
    virtual void createSession(const std::string& tokenHash, const std::string& studentId,
                               Timestamp expiresAt) = 0;
    virtual std::optional<std::string> findSession(const std::string& tokenHash, Timestamp now) const = 0;
    virtual void deleteSession(const std::string& tokenHash) = 0;
};

}  // namespace cramberry
