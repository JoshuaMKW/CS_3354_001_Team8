#include "user/UserController.h"

#include <algorithm>
#include <cctype>

namespace cramberry {

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

const Error kBadCredentials{ErrorCode::Unauthorized, "Email or password is incorrect", {}};

}  // namespace

std::string normalizeEmail(std::string email) {
    email = trim(email);
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return email;
}

UserController::UserController(IUserRepository& users, int passwordIterations)
    : users_(users),
      passwordIterations_(passwordIterations),
      dummyHash_(crypto::hashPassword("not-a-real-password", passwordIterations)) {}

std::optional<std::string> UserController::validateName(const std::string& name) {
    const auto trimmed = trim(name);
    if (trimmed.empty()) return "Enter your name";
    if (trimmed.size() > 100) return "Name must be 100 characters or fewer";
    return std::nullopt;
}

std::optional<std::string> UserController::validateEmail(const std::string& email) {
    const auto at = email.find('@');
    const bool looksValid = email.size() <= 254 && at != std::string::npos && at > 0 &&
                            email.find('.', at) != std::string::npos && email.back() != '.' &&
                            email.find(' ') == std::string::npos &&
                            email.find('@', at + 1) == std::string::npos;
    if (!looksValid) return "Enter a valid email address";
    return std::nullopt;
}

std::optional<std::string> UserController::validatePassword(const std::string& password) {
    if (password.size() < 8) return "Password must be at least 8 characters";
    if (password.size() > 128) return "Password must be 128 characters or fewer";
    const bool hasLetter = std::any_of(password.begin(), password.end(),
                                       [](unsigned char c) { return std::isalpha(c); });
    const bool hasDigit = std::any_of(password.begin(), password.end(),
                                      [](unsigned char c) { return std::isdigit(c); });
    if (!hasLetter || !hasDigit) return "Password must contain at least one letter and one number";
    return std::nullopt;
}

std::optional<std::string> UserController::validateUtcOffset(int minutes) {
    if (minutes < -12 * 60 || minutes > 14 * 60) return "UTC offset must be between -720 and 840 minutes";
    return std::nullopt;
}

Outcome<SignedIn> UserController::registerStudent(const std::string& name, const std::string& email,
                                                  const std::string& password, int utcOffsetMinutes,
                                                  Timestamp now) {
    const std::string cleanEmail = normalizeEmail(email);
    if (auto e = validateName(name)) return invalid(*e);
    if (auto e = validateEmail(cleanEmail)) return invalid(*e);
    if (auto e = validatePassword(password)) return invalid(*e);
    if (auto e = validateUtcOffset(utcOffsetMinutes)) return invalid(*e);

    Student student{crypto::newId(), trim(name), cleanEmail, utcOffsetMinutes, now};
    if (!users_.create(student, crypto::hashPassword(password, passwordIterations_))) {
        return Error{ErrorCode::Conflict, "An account with this email already exists", {}};
    }
    return startSession(student, now);
}

Outcome<SignedIn> UserController::login(const std::string& email, const std::string& password,
                                        Timestamp now) {
    const auto found = users_.findByEmail(normalizeEmail(email));
    if (!found) {
        crypto::verifyPassword(password, dummyHash_);  // same cost as a real check
        return kBadCredentials;
    }
    if (!crypto::verifyPassword(password, found->passwordHash)) return kBadCredentials;
    return startSession(found->student, now);
}

Outcome<SignedIn> UserController::startSession(const Student& student, Timestamp now) {
    SignedIn session{crypto::randomToken(), student, now + kSessionMinutes};
    users_.createSession(crypto::sha256Hex(session.token), student.id, session.expiresAt);
    return session;
}

void UserController::logout(const std::string& token) {
    users_.deleteSession(crypto::sha256Hex(token));
}

std::optional<std::string> UserController::authenticate(const std::string& token,
                                                        Timestamp now) const {
    if (token.size() != 64) return std::nullopt;
    return users_.findSession(crypto::sha256Hex(token), now);
}

Outcome<Student> UserController::profile(const std::string& studentId) const {
    auto student = users_.findById(studentId);
    if (!student) return notFound("Account not found");
    return *student;
}

Outcome<Student> UserController::updateProfile(const std::string& studentId,
                                               const ProfileUpdate& update) {
    auto student = users_.findById(studentId);
    if (!student) return notFound("Account not found");
    if (update.name) {
        if (auto e = validateName(*update.name)) return invalid(*e);
        student->name = trim(*update.name);
    }
    if (update.utcOffsetMinutes) {
        if (auto e = validateUtcOffset(*update.utcOffsetMinutes)) return invalid(*e);
        student->utcOffsetMinutes = *update.utcOffsetMinutes;
    }
    users_.update(*student);
    return *student;
}

Outcome<Done> UserController::deleteAccount(const std::string& studentId,
                                            const std::string& password) {
    const auto hash = users_.passwordHash(studentId);
    if (!hash) return notFound("Account not found");
    if (!crypto::verifyPassword(password, *hash)) {
        return Error{ErrorCode::Unauthorized, "Password is incorrect", {}};
    }
    users_.remove(studentId);
    return Done{};
}

}  // namespace cramberry
