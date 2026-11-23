#pragma once

#include <string>

namespace cramberry::crypto {

// NFR-9: passwords are stored in one fixed format,
//   pbkdf2_sha256$<iterations>$<base64 salt>$<base64 hash>
// using PBKDF2-HMAC-SHA256 with a random 16-byte salt.
constexpr int kDefaultPasswordIterations = 600000;  // OWASP 2023 guidance for PBKDF2-SHA256

std::string hashPassword(const std::string& password, int iterations = kDefaultPasswordIterations);
// Constant-time comparison. Returns false for malformed hashes.
bool verifyPassword(const std::string& password, const std::string& stored);

// 32 random bytes as 64 hex characters, for session tokens.
std::string randomToken();
std::string sha256Hex(const std::string& data);
// Random UUID v4 string, used as the primary key of every row.
std::string newId();

}  // namespace cramberry::crypto
