// Pure-logic tests: Validation + AuthService, using in-memory fakes (no SQLite, no libsodium).
#include <gtest/gtest.h>

#include <map>
#include <vector>

#include "user/AuthService.h"
#include "user/Validation.h"

using namespace cramberry;

namespace {

class FakeCrypto : public ICrypto {
public:
    std::string hashPassword(const std::string& pw) const override { return "hash:" + pw; }
    bool verifyPassword(const std::string& enc, const std::string& pw) const override { return enc == "hash:" + pw; }
    std::string randomToken() const override { return "token" + std::to_string(++counter_); }
    std::string tokenDigest(const std::string& t) const override { return "digest:" + t; }
private:
    mutable int counter_ = 0;
};

class FakeRepo : public IUserRepository {
public:
    CreateResult createUser(User& user) override {
        for (const auto& [id, u] : users_) {
            if (u.email == user.email) return CreateResult::EmailTaken;
            if (u.username == user.username) return CreateResult::UsernameTaken;
        }
        user.id = ++nextId_;
        users_[user.id] = user;
        return CreateResult::Created;
    }
    std::optional<User> findByEmail(const std::string& email) override {
        for (const auto& [id, u] : users_) if (u.email == email) return u;
        return std::nullopt;
    }
    std::optional<User> findById(std::int64_t id) override {
        auto it = users_.find(id);
        if (it == users_.end()) return std::nullopt;
        return it->second;
    }
    void setStatus(std::int64_t id, AccountStatus s) override { users_[id].status = s; }
    void recordLoginAttempt(const std::string& email, bool success, const std::string&, std::int64_t now) override {
        attempts_.push_back({email, success, now});
    }
    int countRecentFailures(const std::string& email, std::int64_t since) override {
        int count = 0;
        for (const auto& a : attempts_) {
            if (a.email != email) continue;
            if (a.success) count = 0;                       // a success resets the streak
            else if (a.at >= since) ++count;
        }
        return count;
    }
    void createSession(const std::string& d, std::int64_t userId, std::int64_t expires) override {
        sessions_[d] = {userId, expires};
    }
    std::optional<std::int64_t> findSessionUser(const std::string& d, std::int64_t now) override {
        auto it = sessions_.find(d);
        if (it == sessions_.end() || it->second.second <= now) return std::nullopt;
        return it->second.first;
    }
    void deleteSession(const std::string& d) override { sessions_.erase(d); }
    void deleteExpiredSessions(std::int64_t now) override {
        for (auto it = sessions_.begin(); it != sessions_.end();)
            it = (it->second.second <= now) ? sessions_.erase(it) : std::next(it);
    }

    struct Attempt { std::string email; bool success; std::int64_t at; };
    std::map<std::int64_t, User> users_;
    std::vector<Attempt> attempts_;
    std::map<std::string, std::pair<std::int64_t, std::int64_t>> sessions_;
private:
    std::int64_t nextId_ = 0;
};

struct Fixture {
    FakeRepo repo;
    FakeCrypto crypto;
    std::int64_t now = 1'000'000;
    AuthService auth{repo, crypto, AuthConfig{}, [this] { return now; }};

    AuthResult signupDefault() { return auth.signup("Name@Example.com ", "name_n", "Password12#"); }
};

}  // namespace

// ---- Validation ----
TEST(Validation, PasswordRules) {
    EXPECT_TRUE(validatePassword("Password12#").ok);
    EXPECT_FALSE(validatePassword("Pass1#").ok);          // too short
    EXPECT_FALSE(validatePassword("Password##").ok);      // no number
    EXPECT_FALSE(validatePassword("Password123").ok);     // no special character
    EXPECT_FALSE(validatePassword(std::string(129, 'a') + "1#").ok);  // too long
}

TEST(Validation, EmailRules) {
    EXPECT_TRUE(validateEmail("a@b.co").ok);
    EXPECT_FALSE(validateEmail("").ok);
    EXPECT_FALSE(validateEmail("no-at-sign.com").ok);
    EXPECT_FALSE(validateEmail("two@@ats.com").ok);
    EXPECT_FALSE(validateEmail("a@nodot").ok);
    EXPECT_FALSE(validateEmail("sp ace@x.com").ok);
}

TEST(Validation, UsernameRules) {
    EXPECT_TRUE(validateUsername("name_n.1").ok);
    EXPECT_FALSE(validateUsername("ab").ok);
    EXPECT_FALSE(validateUsername("has space").ok);
    EXPECT_FALSE(validateUsername(std::string(31, 'a')).ok);
}

TEST(Validation, NormalizeEmail) { EXPECT_EQ(normalizeEmail("  Name@Example.COM "), "name@example.com"); }

// ---- Signup ----
TEST(Signup, CreatesAccountAndSession) {
    Fixture f;
    AuthResult r = f.signupDefault();
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.user->email, "name@example.com");      // normalized
    EXPECT_FALSE(r.sessionToken.empty());
    EXPECT_TRUE(r.user->passwordHash.empty());          // never handed back
    EXPECT_NE(f.repo.users_.begin()->second.passwordHash, "Password12#");  // stored as a hash
}

TEST(Signup, RejectsWeakPasswordAndStoresNothing) {
    Fixture f;
    AuthResult r = f.auth.signup("a@b.com", "someone", "password");
    EXPECT_EQ(r.error, AuthError::InvalidInput);
    EXPECT_TRUE(f.repo.users_.empty());
}

TEST(Signup, RejectsDuplicates) {
    Fixture f;
    ASSERT_TRUE(f.signupDefault().ok());
    EXPECT_EQ(f.auth.signup("Name@example.com", "other_name", "Password12#").error, AuthError::EmailTaken);
    EXPECT_EQ(f.auth.signup("new@example.com", "name_n", "Password12#").error, AuthError::UsernameTaken);
}

// ---- Login ----
TEST(Login, CorrectCredentialsSucceed) {
    Fixture f;
    f.signupDefault();
    AuthResult r = f.auth.login("name@example.com", "Password12#", "1.2.3.4");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.user->username, "name_n");
    EXPECT_FALSE(r.sessionToken.empty());
}

TEST(Login, WrongPasswordAndUnknownEmailLookIdentical) {
    Fixture f;
    f.signupDefault();
    AuthResult wrongPw = f.auth.login("name@example.com", "Wrong12#", "ip");
    AuthResult noUser = f.auth.login("nobody@example.com", "Password12#", "ip");
    EXPECT_EQ(wrongPw.error, AuthError::BadCredentials);
    EXPECT_EQ(noUser.error, AuthError::BadCredentials);
    EXPECT_EQ(wrongPw.message, noUser.message);
    EXPECT_TRUE(wrongPw.sessionToken.empty());
}

TEST(Login, LocksAfterFiveFailuresThenRecovers) {
    Fixture f;
    f.signupDefault();
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(f.auth.login("name@example.com", "Wrong12#", "ip").error, AuthError::BadCredentials);
    // Even the right password is refused while locked.
    EXPECT_EQ(f.auth.login("name@example.com", "Password12#", "ip").error, AuthError::RateLimited);
    f.now += 16 * 60;  // window passes
    EXPECT_TRUE(f.auth.login("name@example.com", "Password12#", "ip").ok());
}

TEST(Login, SuccessResetsFailureCount) {
    Fixture f;
    f.signupDefault();
    for (int i = 0; i < 4; ++i) f.auth.login("name@example.com", "Wrong12#", "ip");
    ASSERT_TRUE(f.auth.login("name@example.com", "Password12#", "ip").ok());
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(f.auth.login("name@example.com", "Wrong12#", "ip").error, AuthError::BadCredentials);
}

TEST(Login, DeactivatedAccountIsBlocked) {
    Fixture f;
    AuthResult r = f.signupDefault();
    f.repo.setStatus(r.user->id, AccountStatus::Deactivated);
    EXPECT_EQ(f.auth.login("name@example.com", "Password12#", "ip").error, AuthError::Deactivated);
    EXPECT_FALSE(f.auth.userForSession(r.sessionToken).has_value());  // existing sessions stop working too
}

TEST(Login, EveryAttemptIsLogged) {
    Fixture f;
    f.signupDefault();
    f.auth.login("name@example.com", "Wrong12#", "ip");
    f.auth.login("name@example.com", "Password12#", "ip");
    ASSERT_EQ(f.repo.attempts_.size(), 2u);
    EXPECT_FALSE(f.repo.attempts_[0].success);
    EXPECT_TRUE(f.repo.attempts_[1].success);
}

// ---- Sessions ----
TEST(Session, ValidThenLoggedOut) {
    Fixture f;
    AuthResult r = f.signupDefault();
    ASSERT_TRUE(f.auth.userForSession(r.sessionToken).has_value());
    f.auth.logout(r.sessionToken);
    EXPECT_FALSE(f.auth.userForSession(r.sessionToken).has_value());
}

TEST(Session, ExpiresAndRejectsGarbage) {
    Fixture f;
    AuthResult r = f.signupDefault();
    EXPECT_FALSE(f.auth.userForSession("not-a-token").has_value());
    EXPECT_FALSE(f.auth.userForSession("").has_value());
    f.now += f.auth.sessionSeconds() + 1;
    EXPECT_FALSE(f.auth.userForSession(r.sessionToken).has_value());
}

TEST(Session, TokenIsNotStoredInTheClear) {
    Fixture f;
    AuthResult r = f.signupDefault();
    EXPECT_EQ(f.repo.sessions_.count(r.sessionToken), 0u);
    EXPECT_EQ(f.repo.sessions_.count("digest:" + r.sessionToken), 1u);
}
