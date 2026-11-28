#include "user/AuthService.h"

#include <ctime>
#include <utility>

#include "user/Validation.h"

namespace cramberry {
namespace {

AuthResult failure(AuthError error, const std::string& message) {
    AuthResult result;
    result.error = error;
    result.message = message;
    return result;
}

}  // namespace

AuthService::AuthService(IUserRepository& repository, ICrypto& crypto, AuthConfig config, Clock clock)
    : repo_(repository), crypto_(crypto), config_(config), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
    dummyHash_ = crypto_.hashPassword("cramberry-dummy-password-1!");
}

AuthResult AuthService::signup(const std::string& rawEmail, const std::string& rawUsername,
                               const std::string& password) {
    const std::string email = normalizeEmail(rawEmail);
    const std::string username = trim(rawUsername);

    const ValidationResult checks[] = {validateEmail(email), validateUsername(username),
                                       validatePassword(password)};
    for (const ValidationResult& check : checks) {
        if (!check.ok) return failure(AuthError::InvalidInput, check.message);
    }

    const std::int64_t now = clock_();
    User user;
    user.email = email;
    user.username = username;
    user.passwordHash = crypto_.hashPassword(password);
    user.createdAt = now;

    switch (repo_.createUser(user)) {
        case CreateResult::EmailTaken:
            return failure(AuthError::EmailTaken, "An account with that email already exists.");
        case CreateResult::UsernameTaken:
            return failure(AuthError::UsernameTaken, "That username is taken.");
        case CreateResult::Created:
            break;
    }
    return startSession(std::move(user), now);
}

AuthResult AuthService::login(const std::string& rawEmail, const std::string& password,
                              const std::string& ip) {
    const std::string email = normalizeEmail(rawEmail);
    const std::int64_t now = clock_();
    // One message for every kind of credential failure, so nobody can probe which emails exist.
    const AuthResult badCredentials =
        failure(AuthError::BadCredentials, "Incorrect email or password.");

    if (email.empty() || email.size() > 254 || password.empty() || password.size() > kMaxPasswordLength) {
        return badCredentials;
    }

    if (repo_.countRecentFailures(email, now - config_.windowSeconds) >= config_.maxFailures) {
        return failure(AuthError::RateLimited,
                       "Too many failed attempts. Please wait a few minutes and try again.");
    }

    std::optional<User> user = repo_.findByEmail(email);
    // Always do one hash verification, even for unknown emails, to keep response times similar.
    const bool passwordOk =
        crypto_.verifyPassword(user ? user->passwordHash : dummyHash_, password) && user.has_value();

    if (!passwordOk) {
        repo_.recordLoginAttempt(email, false, ip, now);
        return badCredentials;
    }
    if (user->status == AccountStatus::Deactivated) {
        repo_.recordLoginAttempt(email, false, ip, now);
        return failure(AuthError::Deactivated,
                       "This account has been deactivated. Contact an administrator.");
    }

    repo_.recordLoginAttempt(email, true, ip, now);
    return startSession(std::move(*user), now);
}

AuthResult AuthService::startSession(User user, std::int64_t now) {
    repo_.deleteExpiredSessions(now);
    const std::string token = crypto_.randomToken();
    // Only a digest of the token is stored, so a leaked database can't be used to hijack sessions.
    repo_.createSession(crypto_.tokenDigest(token), user.id, now + config_.sessionSeconds);

    user.passwordHash.clear();
    AuthResult result;
    result.sessionToken = token;
    result.user = std::move(user);
    return result;
}

std::optional<User> AuthService::userForSession(const std::string& token) {
    if (token.empty()) return std::nullopt;
    const std::optional<std::int64_t> userId =
        repo_.findSessionUser(crypto_.tokenDigest(token), clock_());
    if (!userId) return std::nullopt;

    std::optional<User> user = repo_.findById(*userId);
    if (!user || user->status == AccountStatus::Deactivated) return std::nullopt;
    user->passwordHash.clear();
    return user;
}

void AuthService::logout(const std::string& token) {
    if (!token.empty()) repo_.deleteSession(crypto_.tokenDigest(token));
}

}  // namespace cramberry
