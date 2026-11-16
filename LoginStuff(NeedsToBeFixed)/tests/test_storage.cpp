// Tests for the real SQLite repository and libsodium crypto (needs both libraries).
#include <gtest/gtest.h>

#include "user/Crypto.h"
#include "user/UserRepository.h"

using namespace cramberry;

namespace {
User makeUser(const std::string& email, const std::string& username) {
    User u;
    u.email = email;
    u.username = username;
    u.passwordHash = "hash";
    u.createdAt = 100;
    return u;
}
}  // namespace

TEST(SqliteRepo, CreateAndFind) {
    SqliteUserRepository repo(":memory:");
    User u = makeUser("a@b.com", "alice");
    ASSERT_EQ(repo.createUser(u), CreateResult::Created);
    EXPECT_GT(u.id, 0);
    ASSERT_TRUE(repo.findByEmail("a@b.com").has_value());
    EXPECT_EQ(repo.findById(u.id)->username, "alice");
    EXPECT_FALSE(repo.findByEmail("missing@b.com").has_value());
}

TEST(SqliteRepo, UniqueEmailAndUsernameCaseInsensitive) {
    SqliteUserRepository repo(":memory:");
    User a = makeUser("a@b.com", "alice");
    ASSERT_EQ(repo.createUser(a), CreateResult::Created);
    User sameEmail = makeUser("a@b.com", "other");
    User sameName = makeUser("c@d.com", "ALICE");
    EXPECT_EQ(repo.createUser(sameEmail), CreateResult::EmailTaken);
    EXPECT_EQ(repo.createUser(sameName), CreateResult::UsernameTaken);
}

TEST(SqliteRepo, StatusRoundTrip) {
    SqliteUserRepository repo(":memory:");
    User u = makeUser("a@b.com", "alice");
    repo.createUser(u);
    repo.setStatus(u.id, AccountStatus::Deactivated);
    EXPECT_EQ(repo.findById(u.id)->status, AccountStatus::Deactivated);
}

TEST(SqliteRepo, FailureCountWindowAndReset) {
    SqliteUserRepository repo(":memory:");
    repo.recordLoginAttempt("a@b.com", false, "ip", 100);
    repo.recordLoginAttempt("a@b.com", false, "ip", 200);
    repo.recordLoginAttempt("other@b.com", false, "ip", 200);
    EXPECT_EQ(repo.countRecentFailures("a@b.com", 0), 2);
    EXPECT_EQ(repo.countRecentFailures("a@b.com", 150), 1);   // old one outside window
    repo.recordLoginAttempt("a@b.com", true, "ip", 300);
    EXPECT_EQ(repo.countRecentFailures("a@b.com", 0), 0);     // success resets
    repo.recordLoginAttempt("a@b.com", false, "ip", 400);
    EXPECT_EQ(repo.countRecentFailures("a@b.com", 0), 1);
}

TEST(SqliteRepo, SessionLifecycleAndCascade) {
    SqliteUserRepository repo(":memory:");
    User u = makeUser("a@b.com", "alice");
    repo.createUser(u);
    repo.createSession("digest1", u.id, 1000);
    EXPECT_EQ(repo.findSessionUser("digest1", 999), u.id);
    EXPECT_FALSE(repo.findSessionUser("digest1", 1000).has_value());  // expired
    repo.deleteSession("digest1");
    EXPECT_FALSE(repo.findSessionUser("digest1", 1).has_value());
}

TEST(SodiumCryptoTest, HashVerifyAndTokens) {
    SodiumCrypto crypto;
    const std::string hash = crypto.hashPassword("Password12#");
    EXPECT_NE(hash, "Password12#");
    EXPECT_EQ(hash.rfind("$argon2id$", 0), 0u);
    EXPECT_NE(hash, crypto.hashPassword("Password12#"));  // random salt each time
    EXPECT_TRUE(crypto.verifyPassword(hash, "Password12#"));
    EXPECT_FALSE(crypto.verifyPassword(hash, "password12#"));

    const std::string a = crypto.randomToken(), b = crypto.randomToken();
    EXPECT_EQ(a.size(), 64u);
    EXPECT_NE(a, b);
    EXPECT_EQ(crypto.tokenDigest(a), crypto.tokenDigest(a));
    EXPECT_NE(crypto.tokenDigest(a), a);
}
