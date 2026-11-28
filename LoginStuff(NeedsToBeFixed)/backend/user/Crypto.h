#pragma once
// Everything that needs a cryptographic library lives behind this interface,
// so the login logic can be unit-tested without one.
#include <string>

namespace cramberry {

class ICrypto {
public:
    virtual ~ICrypto() = default;
    virtual std::string hashPassword(const std::string& password) const = 0;  // salted, self-describing
    virtual bool verifyPassword(const std::string& encodedHash, const std::string& password) const = 0;
    virtual std::string randomToken() const = 0;                              // for session cookies
    virtual std::string tokenDigest(const std::string& token) const = 0;      // what we store instead of the token
};

// libsodium: Argon2id password hashing, OS-provided randomness, BLAKE2b digests.
class SodiumCrypto : public ICrypto {
public:
    SodiumCrypto();  // throws std::runtime_error if libsodium cannot initialise
    std::string hashPassword(const std::string& password) const override;
    bool verifyPassword(const std::string& encodedHash, const std::string& password) const override;
    std::string randomToken() const override;
    std::string tokenDigest(const std::string& token) const override;
};

}  // namespace cramberry
