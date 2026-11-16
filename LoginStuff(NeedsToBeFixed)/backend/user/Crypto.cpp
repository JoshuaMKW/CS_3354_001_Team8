#include "user/Crypto.h"

#include <sodium.h>

#include <stdexcept>

namespace cramberry {

SodiumCrypto::SodiumCrypto() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium failed to initialise");
}

std::string SodiumCrypto::hashPassword(const std::string& password) const {
    char encoded[crypto_pwhash_STRBYTES];
    // crypto_pwhash_str generates a random salt and uses Argon2id.
    if (crypto_pwhash_str(encoded, password.c_str(), password.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("password hashing failed (out of memory)");
    }
    return std::string(encoded);
}

bool SodiumCrypto::verifyPassword(const std::string& encodedHash, const std::string& password) const {
    return crypto_pwhash_str_verify(encodedHash.c_str(), password.c_str(), password.size()) == 0;
}

std::string SodiumCrypto::randomToken() const {
    unsigned char bytes[32];
    randombytes_buf(bytes, sizeof bytes);
    char hex[sizeof bytes * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, bytes, sizeof bytes);
    return std::string(hex);
}

std::string SodiumCrypto::tokenDigest(const std::string& token) const {
    unsigned char digest[32];
    crypto_generichash(digest, sizeof digest,
                       reinterpret_cast<const unsigned char*>(token.data()), token.size(),
                       nullptr, 0);
    char hex[sizeof digest * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, digest, sizeof digest);
    return std::string(hex);
}

}  // namespace cramberry
