#pragma once
#include <cstdint>
#include <string>

namespace cramberry {

enum class AccountStatus { Active, Deactivated };

struct User {
    std::int64_t id = 0;
    std::string email;         // stored lower-case
    std::string username;
    std::string passwordHash;  // encoded Argon2id string; the password itself is never stored
    AccountStatus status = AccountStatus::Active;
    std::int64_t createdAt = 0;  // unix seconds
};

}  // namespace cramberry
