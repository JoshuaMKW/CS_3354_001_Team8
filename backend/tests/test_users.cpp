// User registration, sign-in and account deletion (FR-1, FR-2, FR-14, NFR-7, NFR-9).

#include <gmock/gmock.h>

#include "tests/ApiFixture.h"
#include "user/UserController.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;
using ::testing::HasSubstr;
using ::testing::StartsWith;

class Users : public ApiFixture {
protected:
    UserController controller{users, /*passwordIterations=*/1000};
};

TEST(Crypto, PasswordHashUsesTheFixedFormatAndVerifies) {
    const auto hash = crypto::hashPassword("correct horse 1", 1000);
    EXPECT_THAT(hash, StartsWith("pbkdf2_sha256$1000$"));
    EXPECT_TRUE(crypto::verifyPassword("correct horse 1", hash));
    EXPECT_FALSE(crypto::verifyPassword("correct horse 2", hash));
    EXPECT_NE(hash, crypto::hashPassword("correct horse 1", 1000)) << "salt must be random";
    EXPECT_FALSE(crypto::verifyPassword("x", "not-a-hash"));
    EXPECT_FALSE(crypto::verifyPassword("x", "pbkdf2_sha256$abc$$"));
}

TEST(Crypto, IdsAndTokensAreRandomAndWellFormed) {
    const auto id = crypto::newId();
    EXPECT_EQ(id.size(), 36u);
    EXPECT_EQ(id[14], '4');
    EXPECT_NE(id, crypto::newId());
    EXPECT_EQ(crypto::randomToken().size(), 64u);
}

TEST_F(Users, RegisterThenLogInAndOut) {
    const auto registered = controller.registerStudent("Ada", "Ada@Example.com ", "password1", -360, kNow);
    ASSERT_TRUE(registered.ok()) << registered.error().message;
    EXPECT_EQ(registered.value().student.email, "ada@example.com");
    EXPECT_EQ(controller.authenticate(registered.value().token, kNow), registered.value().student.id);

    const auto login = controller.login("ADA@example.com", "password1", kNow);
    ASSERT_TRUE(login.ok());
    EXPECT_NE(login.value().token, registered.value().token);

    controller.logout(login.value().token);
    EXPECT_FALSE(controller.authenticate(login.value().token, kNow));
    EXPECT_TRUE(controller.authenticate(registered.value().token, kNow)) << "other sessions stay valid";
}

TEST_F(Users, SessionsExpireAfterSevenDays) {
    const auto s = controller.registerStudent("Ada", "ada@example.com", "password1", 0, kNow);
    ASSERT_TRUE(s.ok());
    EXPECT_TRUE(controller.authenticate(s.value().token, kNow + 7 * kDay - 1));
    EXPECT_FALSE(controller.authenticate(s.value().token, kNow + 7 * kDay));
}

TEST_F(Users, RejectsBadInputAndDuplicateEmail) {
    EXPECT_EQ(controller.registerStudent("", "a@b.co", "password1", 0, kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(controller.registerStudent("A", "not-an-email", "password1", 0, kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(controller.registerStudent("A", "a@b.co", "short1", 0, kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(controller.registerStudent("A", "a@b.co", "lettersonly", 0, kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(controller.registerStudent("A", "a@b.co", "password1", 99999, kNow).error().code, ErrorCode::InvalidInput);

    ASSERT_TRUE(controller.registerStudent("A", "a@b.co", "password1", 0, kNow).ok());
    const auto again = controller.registerStudent("B", "A@B.CO", "password2", 0, kNow);
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.error().code, ErrorCode::Conflict);
}

TEST_F(Users, WrongPasswordAndUnknownEmailLookTheSame) {
    ASSERT_TRUE(controller.registerStudent("A", "a@b.co", "password1", 0, kNow).ok());
    const auto wrongPassword = controller.login("a@b.co", "password2", kNow);
    const auto unknownEmail = controller.login("nobody@b.co", "password1", kNow);
    ASSERT_FALSE(wrongPassword.ok());
    ASSERT_FALSE(unknownEmail.ok());
    EXPECT_EQ(wrongPassword.error().code, ErrorCode::Unauthorized);
    EXPECT_EQ(wrongPassword.error().message, unknownEmail.error().message);
}

TEST_F(Users, UpdateProfileValidates) {
    const auto s = controller.registerStudent("Ada", "ada@example.com", "password1", 0, kNow);
    ASSERT_TRUE(s.ok());
    const auto id = s.value().student.id;
    const auto updated = controller.updateProfile(id, {std::string("Ada L."), -300});
    ASSERT_TRUE(updated.ok());
    EXPECT_EQ(users.findById(id)->name, "Ada L.");
    EXPECT_EQ(users.findById(id)->utcOffsetMinutes, -300);
    EXPECT_FALSE(controller.updateProfile(id, {std::string("  "), std::nullopt}).ok());
}

// FR-14 / NFR-7: deleting an account needs the password and removes every row it owns,
// without touching another student's data.
TEST_F(Users, DeleteAccountRemovesEverythingItOwns) {
    const auto a = controller.registerStudent("A", "a@b.co", "password1", 0, kNow);
    const auto b = controller.registerStudent("B", "b@b.co", "password1", 0, kNow);
    ASSERT_TRUE(a.ok() && b.ok());
    const auto idA = a.value().student.id, idB = b.value().student.id;
    addTask(idA, "A's essay", kNow + kDay);
    addTask(idB, "B's essay", kNow + kDay);
    ASSERT_TRUE(settings.save(idA, weekdayEvenings()));
    schedules.replace(idA, {{"A's essay", "x", {Day::Mon, hm(16), hm(17)}}});

    EXPECT_EQ(controller.deleteAccount(idA, "wrong-password1").error().code, ErrorCode::Unauthorized);
    EXPECT_TRUE(users.findById(idA));

    ASSERT_TRUE(controller.deleteAccount(idA, "password1").ok());
    EXPECT_FALSE(users.findById(idA));
    EXPECT_FALSE(controller.authenticate(a.value().token, kNow));
    EXPECT_TRUE(taskRepo.listTasks(idA, {}).empty());
    EXPECT_FALSE(settings.find(idA));
    EXPECT_TRUE(schedules.getActiveStudyItems(idA).empty());
    EXPECT_EQ(db->exec("SELECT COUNT(*) AS n FROM tasks")[0]["n"].as<int>(), 1);
    EXPECT_EQ(taskRepo.listTasks(idB, {}).size(), 1u);
}

}  // namespace
}  // namespace cramberry
