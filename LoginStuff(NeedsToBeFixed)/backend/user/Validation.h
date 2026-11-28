#pragma once
#include <string>

namespace cramberry {

constexpr std::size_t kMaxPasswordLength = 128;  // also stops giant inputs from burning CPU in the hasher

struct ValidationResult {
    bool ok = true;
    std::string message;
};

std::string trim(const std::string& text);
std::string normalizeEmail(const std::string& raw);  // trim + lower-case

ValidationResult validateEmail(const std::string& email);
ValidationResult validateUsername(const std::string& username);
// At least 8 characters, one number, one special character (matches the group's UC-F01 rule).
ValidationResult validatePassword(const std::string& password);

}  // namespace cramberry
