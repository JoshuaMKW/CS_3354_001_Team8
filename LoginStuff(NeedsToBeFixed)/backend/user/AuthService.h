#pragma once
// All the signup/login rules in one place. No HTTP, no SQL, no crypto library here.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "user/Crypto.h"
#include "user/User.h"
#include "user/UserRepository.h"

namespace cramberry {

enum class AuthError {
    None,
    InvalidInput,
    EmailTaken,
    UsernameTaken,
    BadCredentials,
    RateLimited,
    Deactivated
};

struct AuthResult {
    AuthError error = AuthError::None;
    std::string message;       // safe to show to the user
    std::string sessionToken;  // set on success; goes in the cookie
    std::optional<User> user;  // set on success; passwordHash is blanked
    bool ok() const { return error == AuthError::None; }
};

struct AuthConfig {
    int maxFailures = 5;                  // failed logins allowed...
    int windowSeconds = 15 * 60;          // ...within this window
    int sessionSeconds = 7 * 24 * 3600;   // session lifetime
};

class AuthService {
public:
    using Clock = std::function<std::int64_t()>;  // unix seconds; injectable for tests

    AuthService(IUserRepository& repository, ICrypto& crypto, AuthConfig config = {},
                Clock clock = nullptr);

    AuthResult signup(const std::string& email, const std::string& username,
                      const std::string& password);
    AuthResult login(const std::string& email, const std::string& password, const std::string& ip);

    std::optional<User> userForSession(const std::string& token);
    void logout(const std::string& token);

    int sessionSeconds() const { return config_.sessionSeconds; }

private:
    AuthResult startSession(User user, std::int64_t now);

    IUserRepository& repo_;
    ICrypto& crypto_;
    AuthConfig config_;
    Clock clock_;
    std::string dummyHash_;  // verified against when the email is unknown, so timing doesn't reveal it
};

}  // namespace cramberry
