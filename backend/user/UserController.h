#pragma once

#include <optional>
#include <string>

#include "common/Outcome.h"
#include "user/Crypto.h"
#include "user/User.h"

namespace cramberry {

struct SignedIn {
    std::string token;  // give this back as "Authorization: Bearer <token>"
    Student student;
    Timestamp expiresAt = 0;
};

struct ProfileUpdate {
    std::optional<std::string> name;
    std::optional<int> utcOffsetMinutes;
};

// Use cases 2 (log in) and FR-1, FR-2, FR-14: registration, sign-in, sign-out,
// profile changes and account deletion. Knows nothing about HTTP.
class UserController {
public:
    static constexpr Timestamp kSessionMinutes = 7 * 24 * 60;

    explicit UserController(IUserRepository& users,
                            int passwordIterations = crypto::kDefaultPasswordIterations);

    Outcome<SignedIn> registerStudent(const std::string& name, const std::string& email,
                                      const std::string& password, int utcOffsetMinutes,
                                      Timestamp now);
    Outcome<SignedIn> login(const std::string& email, const std::string& password, Timestamp now);
    void logout(const std::string& token);

    // The student a session token belongs to, if it is valid and not expired.
    std::optional<std::string> authenticate(const std::string& token, Timestamp now) const;

    Outcome<Student> profile(const std::string& studentId) const;
    Outcome<Student> updateProfile(const std::string& studentId, const ProfileUpdate& update);
    // Asks for the password again so a stolen session can't delete the account.
    Outcome<Done> deleteAccount(const std::string& studentId, const std::string& password);

    static std::optional<std::string> validateName(const std::string& name);
    static std::optional<std::string> validateEmail(const std::string& email);
    static std::optional<std::string> validatePassword(const std::string& password);
    static std::optional<std::string> validateUtcOffset(int minutes);

private:
    Outcome<SignedIn> startSession(const Student& student, Timestamp now);

    IUserRepository& users_;
    int passwordIterations_;
    std::string dummyHash_;  // checked for unknown emails so both paths take as long
};

std::string normalizeEmail(std::string email);

}  // namespace cramberry
