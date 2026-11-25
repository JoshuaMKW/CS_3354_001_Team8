#pragma once

#include "data-api/Database.h"
#include "user/User.h"

namespace cramberry {

class SqlUserRepository : public IUserRepository {
public:
    explicit SqlUserRepository(const data::Database& db) : db_(db) {}

    std::optional<Student> findById(const std::string& studentId) const override;
    std::optional<StoredCredentials> findByEmail(const std::string& email) const override;
    std::optional<std::string> passwordHash(const std::string& studentId) const override;
    bool create(const Student& student, const std::string& passwordHash) override;
    void update(const Student& student) override;
    void remove(const std::string& studentId) override;

    void createSession(const std::string& tokenHash, const std::string& studentId,
                       Timestamp expiresAt) override;
    std::optional<std::string> findSession(const std::string& tokenHash, Timestamp now) const override;
    void deleteSession(const std::string& tokenHash) override;

private:
    const data::Database& db_;
};

}  // namespace cramberry
