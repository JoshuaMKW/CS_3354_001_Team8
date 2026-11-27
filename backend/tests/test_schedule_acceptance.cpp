// KT acceptance tests for schedule export and schedule sharing (UC-8 and UC-9).

#include <gmock/gmock.h>

#include <stdexcept>

#include "common/Http.h"
#include "schedule/ScheduleController.h"
#include "schedule/ScheduleShareRepository.h"
#include "schedule/ScheduleSharing.h"
#include "tests/ApiFixture.h"
#include "tests/Fakes.h"
#include "user/AuthGuard.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;
using ::testing::HasSubstr;
using ::testing::SizeIs;

drogon::HttpResponsePtr invokeRoute(const AuthGuard::Route& route,
                                    const drogon::HttpRequestPtr& request) {
    drogon::HttpResponsePtr response;
    route(request, [&response](const drogon::HttpResponsePtr& result) { response = result; });
    return response;
}

class ScheduleAcceptanceTest : public ApiFixture {
protected:
    std::string owner = addStudent("owner@utd.edu");
    std::string recipient = addStudent("recipient@utd.edu");
    std::string outsider = addStudent("outsider@utd.edu");
    ScheduleController scheduleController{schedules, settings, taskRepo, users};
    SqlScheduleShareRepository shareRepository{*db};
    RecordingGateway notifications;
    ScheduleSharingController sharing{shareRepository, schedules, users, notifications};
};

TEST_F(ScheduleAcceptanceTest, TC_08_01_ExportsPopulatedSchedule) {
    const auto essay = addTask(owner, "Essay", kNow + 3 * kDay);
    schedules.replace(owner, {{"Essay", essay.id, {Day::Tue, hm(18), hm(19)}}});

    const auto exported = scheduleController.exportIcs(owner, kNow);
    ASSERT_TRUE(exported.ok()) << exported.error().message;
    EXPECT_THAT(exported.value(), HasSubstr("BEGIN:VCALENDAR\r\n"));
    EXPECT_THAT(exported.value(), HasSubstr("DTSTART:20261124T180000Z\r\n"));
    EXPECT_THAT(exported.value(), HasSubstr("SUMMARY:Study: Essay\r\n"));
    EXPECT_THAT(exported.value(), HasSubstr("SUMMARY:Due: Essay\r\n"));
    EXPECT_THAT(exported.value(), HasSubstr("END:VCALENDAR\r\n"));
}

TEST_F(ScheduleAcceptanceTest, TC_08_02_ExportsEmptyScheduleWithoutFailing) {
    const auto exported = scheduleController.exportIcs(owner, kNow);
    ASSERT_TRUE(exported.ok()) << exported.error().message;
    EXPECT_THAT(exported.value(), HasSubstr("BEGIN:VCALENDAR\r\n"));
    EXPECT_THAT(exported.value(), HasSubstr("END:VCALENDAR\r\n"));
    EXPECT_THAT(exported.value(), ::testing::Not(HasSubstr("BEGIN:VEVENT")));
}

TEST_F(ScheduleAcceptanceTest, TC_08_03_RejectsExportWhenLoggedOut) {
    UserController authUsers(users, 1000);
    trantor::ConcurrentTaskQueue slowWork(1, "schedule-acceptance-auth");
    AuthGuard guard(authUsers, slowWork);

    bool handlerCalled = false;
    const auto route = guard([&](const drogon::HttpRequestPtr&, const std::string&) {
        handlerCalled = true;
        return http::noContent();
    });
    const auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);

    const auto response = invokeRoute(route, request);
    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k401Unauthorized);
    EXPECT_FALSE(handlerCalled);
}

TEST_F(ScheduleAcceptanceTest, TC_08_04_ExportFailureReturnsSafeErrorAndPreservesSchedule) {
    class FailingScheduleRepository final : public IScheduleRepository {
    public:
        std::vector<StudyBlock> blocks;

        std::vector<StudyBlock> getActiveStudyItems(const std::string&) const override {
            throw std::runtime_error("simulated calendar generation failure");
        }
        void replace(const std::string&, std::vector<StudyBlock> updated) override {
            blocks = std::move(updated);
        }
    } failingSchedules;
    failingSchedules.blocks = {{"Essay", "task-1", {Day::Mon, hm(16), hm(17)}}};
    const auto originalSchedule = failingSchedules.blocks;
    ScheduleController failingController(failingSchedules, settings, taskRepo, users);

    const auto response = http::safely([&]() -> drogon::HttpResponsePtr {
        const auto result = failingController.exportIcs(owner, kNow);
        if (!result) return http::error(result.error());
        auto file = drogon::HttpResponse::newHttpResponse();
        file->setBody(result.value());
        file->setContentTypeString("text/calendar; charset=utf-8");
        return file;
    });

    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k500InternalServerError);
    EXPECT_THAT(response->getBody(), ::testing::Not(HasSubstr("BEGIN:VCALENDAR")));
    EXPECT_THAT(response->getBody(),
                ::testing::Not(HasSubstr("simulated calendar generation failure")));
    EXPECT_EQ(failingSchedules.blocks, originalSchedule);
}

TEST_F(ScheduleAcceptanceTest, TC_09_01_SharesScheduleWithRegisteredRecipient) {
    schedules.replace(owner, {{"Study session", "task-1", {Day::Mon, hm(15), hm(16)}}});

    const auto result = sharing.shareWith(owner, "RECIPIENT@UTD.EDU", kNow);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().recipientId, recipient);
    ASSERT_THAT(notifications.sent, SizeIs(1));
    EXPECT_EQ(notifications.sent[0].studentId, recipient);
    EXPECT_THAT(notifications.sent[0].body, HasSubstr(result.value().id));
}

TEST_F(ScheduleAcceptanceTest, TC_09_02_RecipientViewsSharedSchedule) {
    const StudyBlock block{"Study session", "task-1", {Day::Mon, hm(15), hm(16)}};
    schedules.replace(owner, {block});
    const auto created = sharing.shareWith(owner, "recipient@utd.edu", kNow);
    ASSERT_TRUE(created.ok());

    const auto view = sharing.viewShared(recipient, created.value().id);
    ASSERT_TRUE(view.ok()) << view.error().message;
    EXPECT_EQ(view.value().share.ownerId, owner);
    EXPECT_THAT(view.value().blocks, ::testing::ElementsAre(block));
}

TEST_F(ScheduleAcceptanceTest, TC_09_03_SharedScheduleReflectsOwnerEditOnRefresh) {
    schedules.replace(owner, {{"Study session", "task-1", {Day::Mon, hm(15), hm(16)}}});
    const auto created = sharing.shareWith(owner, "recipient@utd.edu", kNow);
    ASSERT_TRUE(created.ok());

    const auto initial = sharing.viewShared(recipient, created.value().id);
    ASSERT_TRUE(initial.ok());
    ASSERT_THAT(initial.value().blocks, SizeIs(1));
    EXPECT_EQ(initial.value().blocks[0].time, (TimeRange{Day::Mon, hm(15), hm(16)}));

    schedules.replace(owner, {{"Study session", "task-1", {Day::Tue, hm(16), hm(17)}}});
    const auto refreshed = sharing.viewShared(recipient, created.value().id);
    ASSERT_TRUE(refreshed.ok());
    ASSERT_THAT(refreshed.value().blocks, SizeIs(1));
    EXPECT_EQ(refreshed.value().blocks[0].time, (TimeRange{Day::Tue, hm(16), hm(17)}));
}

TEST_F(ScheduleAcceptanceTest, TC_09_04_RejectsInvalidOrUnregisteredRecipient) {
    EXPECT_EQ(sharing.shareWith(owner, "not-an-email", kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(sharing.shareWith(owner, "missing@utd.edu", kNow).error().code, ErrorCode::NotFound);
    EXPECT_EQ(sharing.shareWith(owner, "owner@utd.edu", kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_TRUE(notifications.sent.empty());
}

TEST_F(ScheduleAcceptanceTest, TC_09_05_DeniesUnauthorizedAndUnauthenticatedSharedScheduleAccess) {
    const auto created = sharing.shareWith(owner, "recipient@utd.edu", kNow);
    ASSERT_TRUE(created.ok());
    EXPECT_EQ(sharing.viewShared(owner, created.value().id).error().code, ErrorCode::NotFound);
    EXPECT_EQ(sharing.viewShared(outsider, created.value().id).error().code, ErrorCode::NotFound);

    UserController authUsers(users, 1000);
    trantor::ConcurrentTaskQueue slowWork(1, "schedule-acceptance-shared-auth");
    AuthGuard guard(authUsers, slowWork);
    bool handlerCalled = false;
    const auto route = guard([&](const drogon::HttpRequestPtr&, const std::string&) {
        handlerCalled = true;
        const auto view = sharing.viewShared(recipient, created.value().id);
        return view ? http::noContent() : http::error(view.error());
    });
    const auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);

    const auto response = invokeRoute(route, request);
    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k401Unauthorized);
    EXPECT_FALSE(handlerCalled);
}

TEST_F(ScheduleAcceptanceTest, TC_09_06_DeletedEventDisappearsFromSharedSchedule) {
    const StudyBlock deleted{"Biology Study Session", "task-1", {Day::Mon, hm(15), hm(16)}};
    const StudyBlock retained{"Physics Study Session", "task-2", {Day::Tue, hm(15), hm(16)}};
    schedules.replace(owner, {deleted, retained});
    const auto created = sharing.shareWith(owner, "recipient@utd.edu", kNow);
    ASSERT_TRUE(created.ok());
    ASSERT_THAT(sharing.viewShared(recipient, created.value().id).value().blocks, SizeIs(2));

    schedules.replace(owner, {retained});
    const auto afterDelete = sharing.viewShared(recipient, created.value().id);
    ASSERT_TRUE(afterDelete.ok());
    EXPECT_THAT(afterDelete.value().blocks, ::testing::ElementsAre(retained));
}

}  // namespace
}  // namespace cramberry
