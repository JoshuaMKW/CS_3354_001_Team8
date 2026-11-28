#include "user/Validation.h"

#include <algorithm>
#include <cctype>

namespace cramberry {
namespace {

ValidationResult invalid(const std::string& message) { return {false, message}; }
bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

}  // namespace

std::string trim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::string normalizeEmail(const std::string& raw) {
    std::string email = trim(raw);
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return email;
}

ValidationResult validateEmail(const std::string& email) {
    const std::string message = "Enter a valid email address.";
    if (email.empty() || email.size() > 254) return invalid(message);
    const auto at = email.find('@');
    if (at == std::string::npos || at == 0 || at != email.rfind('@')) return invalid(message);
    const std::string domain = email.substr(at + 1);
    if (domain.size() < 3 || domain.find('.') == std::string::npos ||
        domain.front() == '.' || domain.back() == '.') {
        return invalid(message);
    }
    for (char c : email) {
        if (isSpace(c) || std::iscntrl(static_cast<unsigned char>(c))) return invalid(message);
    }
    return {};
}

ValidationResult validateUsername(const std::string& username) {
    if (username.size() < 5) return invalid("Username must be at least 5 characters.");
    if (username.size() > 30) return invalid("Username must be 30 characters or fewer.");
    for (char c : username) {
        if (!isAlnum(c) && c != '_' && c != '.') {
            return invalid("Use letters, numbers, underscores, or periods only.");
        }
    }
    return {};
}

ValidationResult validatePassword(const std::string& password) {
    if (password.size() < 8) return invalid("Password must be at least 8 characters.");
    if (password.size() > kMaxPasswordLength) return invalid("Password must be 128 characters or fewer.");
    if (!std::any_of(password.begin(), password.end(), isDigit)) {
        return invalid("Password needs at least one number.");
    }
    if (!std::any_of(password.begin(), password.end(),
                     [](char c) { return !isAlnum(c) && !isSpace(c); })) {
        return invalid("Password needs at least one special character.");
    }
    return {};
}

}  // namespace cramberry
