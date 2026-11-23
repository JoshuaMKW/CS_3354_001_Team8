#include "user/Crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace cramberry::crypto {

namespace {

constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;
constexpr const char* kScheme = "pbkdf2_sha256";

std::vector<unsigned char> randomBytes(std::size_t count) {
    std::vector<unsigned char> bytes(count);
    if (RAND_bytes(bytes.data(), static_cast<int>(count)) != 1) {
        throw std::runtime_error("secure random generator failed");
    }
    return bytes;
}

std::string toHex(const unsigned char* data, std::size_t size) {
    static const char* const digits = "0123456789abcdef";
    std::string out(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0xF];
    }
    return out;
}

std::string base64(const std::vector<unsigned char>& data) {
    std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
    const int length = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data.data(),
                                       static_cast<int>(data.size()));
    out.resize(static_cast<std::size_t>(length));
    return out;
}

bool unbase64(const std::string& text, std::size_t expectedBytes, std::vector<unsigned char>& out) {
    if (text.empty() || text.size() % 4 != 0) return false;
    std::vector<unsigned char> buffer(3 * text.size() / 4);
    const int length = EVP_DecodeBlock(buffer.data(), reinterpret_cast<const unsigned char*>(text.data()),
                                       static_cast<int>(text.size()));
    if (length < 0) return false;
    // EVP_DecodeBlock keeps the padding bytes; strip them.
    std::size_t size = static_cast<std::size_t>(length);
    if (text.ends_with("==")) size -= 2;
    else if (text.ends_with("=")) size -= 1;
    if (size != expectedBytes) return false;
    buffer.resize(size);
    out = std::move(buffer);
    return true;
}

std::vector<unsigned char> derive(const std::string& password, const std::vector<unsigned char>& salt,
                                  int iterations) {
    std::vector<unsigned char> hash(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(), kHashBytes,
                          hash.data()) != 1) {
        throw std::runtime_error("password hashing failed");
    }
    return hash;
}

}  // namespace

std::string hashPassword(const std::string& password, int iterations) {
    if (iterations < 1) throw std::invalid_argument("iterations must be positive");
    const auto salt = randomBytes(kSaltBytes);
    const auto hash = derive(password, salt, iterations);
    return std::string(kScheme) + "$" + std::to_string(iterations) + "$" + base64(salt) + "$" +
           base64(hash);
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    // Split "scheme$iterations$salt$hash".
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= stored.size(); ++i) {
        if (i == stored.size() || stored[i] == '$') {
            parts.push_back(stored.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 4 || parts[0] != kScheme) return false;

    int iterations = 0;
    try {
        iterations = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }
    if (iterations < 1 || iterations > 10'000'000) return false;

    std::vector<unsigned char> salt, expected;
    if (!unbase64(parts[2], kSaltBytes, salt) || !unbase64(parts[3], kHashBytes, expected)) {
        return false;
    }
    const auto actual = derive(password, salt, iterations);
    return CRYPTO_memcmp(actual.data(), expected.data(), kHashBytes) == 0;
}

std::string randomToken() {
    const auto bytes = randomBytes(32);
    return toHex(bytes.data(), bytes.size());
}

std::string sha256Hex(const std::string& data) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
    return toHex(digest, sizeof digest);
}

std::string newId() {
    auto b = randomBytes(16);
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);  // version 4
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);  // RFC 4122 variant
    const std::string hex = toHex(b.data(), b.size());
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

}  // namespace cramberry::crypto
