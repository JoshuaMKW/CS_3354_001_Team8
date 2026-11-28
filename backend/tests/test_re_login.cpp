// James Reid Thompson (RE): login and signup test cases (FR-1, "User logging in").
// Ported from his PR #5 (LoginStuff/tests/test_auth.cpp) onto the app's real login code.
// Kept: his tests of behaviour a student can see. The app's rules apply: a password needs
// 8+ characters with a letter and a number, accounts are identified by email only, and
// sessions last 7 days.

#include <gmock/gmock.h>

#include "tests/ApiFixture.h"
#include "user/UserController.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;

class ReLogin : public ApiFixture {
protected:
    UserController auth{users, /*passwordIterations=*/1000};

    Outcome<SignedIn> signupDefault() {
        return auth.registerStudent("Name N", "Name@Example.COM ", "Password12", 0, kNow);
    }
};

// TC-02-01: signing up creates the account and signs the student in.
TEST_F(ReLogin, TC_02_01_SignupCreatesAccountAndSession) {
    const auto r = signupDefault();
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().student.email, "name@example.com");  // normalized
    EXPECT_FALSE(r.value().token.empty());
    EXPECT_EQ(auth.authenticate(r.value().token, kNow), r.value().student.id);
    EXPECT_NE(users.passwordHash(r.value().student.id), std::optional<std::string>{"Password12"})
        << "the password is stored as a hash";
}

// TC-02-02: a weak password is refused and no account is created.
TEST_F(ReLogin, TC_02_02_WeakPasswordIsRejectedAndNothingIsStored) {
    const auto r = auth.registerStudent("Someone", "a@b.com", "password", 0, kNow);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
    EXPECT_FALSE(users.findByEmail("a@b.com").has_value());
}

// TC-02-03: an email that is already registered can't be used again, whatever its case.
TEST_F(ReLogin, TC_02_03_DuplicateEmailIsRejected) {
    ASSERT_TRUE(signupDefault().ok());
    const auto again = auth.registerStudent("Other", "name@EXAMPLE.com", "Password34", 0, kNow);
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.error().code, ErrorCode::Conflict);
}

// TC-02-04: the right email and password sign the student in.
TEST_F(ReLogin, TC_02_04_CorrectCredentialsSucceed) {
    ASSERT_TRUE(signupDefault().ok());
    const auto r = auth.login("name@example.com", "Password12", kNow);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().student.email, "name@example.com");
    EXPECT_FALSE(r.value().token.empty());
}

// TC-02-05: a wrong password and an unknown email get the same answer, so the app
// doesn't reveal which emails have accounts.
TEST_F(ReLogin, TC_02_05_WrongPasswordAndUnknownEmailLookIdentical) {
    ASSERT_TRUE(signupDefault().ok());
    const auto wrongPw = auth.login("name@example.com", "Wrong1234", kNow);
    const auto noUser = auth.login("nobody@example.com", "Password12", kNow);
    ASSERT_FALSE(wrongPw.ok());
    ASSERT_FALSE(noUser.ok());
    EXPECT_EQ(wrongPw.error().code, ErrorCode::Unauthorized);
    EXPECT_EQ(noUser.error().code, ErrorCode::Unauthorized);
    EXPECT_EQ(wrongPw.error().message, noUser.error().message);
}

// TC-02-06: after logging out, the session no longer works.
TEST_F(ReLogin, TC_02_06_SessionWorksUntilLogout) {
    const auto r = signupDefault();
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(auth.authenticate(r.value().token, kNow).has_value());
    auth.logout(r.value().token);
    EXPECT_FALSE(auth.authenticate(r.value().token, kNow).has_value());
}

// TC-02-07: made-up tokens are refused, and a session expires after 7 days.
TEST_F(ReLogin, TC_02_07_SessionExpiresAndGarbageIsRejected) {
    const auto r = signupDefault();
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(auth.authenticate("not-a-token", kNow).has_value());
    EXPECT_FALSE(auth.authenticate("", kNow).has_value());
    EXPECT_TRUE(auth.authenticate(r.value().token, kNow + UserController::kSessionMinutes - 1).has_value());
    EXPECT_FALSE(auth.authenticate(r.value().token, kNow + UserController::kSessionMinutes).has_value());
}

}  // namespace
}  // namespace cramberry
