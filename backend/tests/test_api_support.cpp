// JSON shapes, Gemini answer handling (UC-12, FR-29), AI fallback (NFR-15) and the
// schedule features built on top: moving blocks (UC-6), export (UC-8), dashboard.

#include <gmock/gmock.h>

#include <stdexcept>

#include "agent/AgentController.h"
#include "agent/GeminiClient.h"
#include "common/Json.h"
#include "schedule/ScheduleShareRepository.h"
#include "schedule/ScheduleSharing.h"
#include "schedule/ScheduleController.h"
#include "tests/Fakes.h"
#include "tests/ApiFixture.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// ---------------------------------------------------------------- JSON

TEST(Json, ParsesAndFormatsTimes) {
    EXPECT_EQ(json::parseClock("18:30"), hm(18, 30));
    EXPECT_EQ(json::parseClock("9:05"), hm(9, 5));
    EXPECT_EQ(json::parseClock("24:00"), kMinutesPerDay);
    EXPECT_FALSE(json::parseClock("24:30"));
    EXPECT_FALSE(json::parseClock("7pm"));
    EXPECT_EQ(json::formatClock(hm(7, 5)), "07:05");

    EXPECT_EQ(json::parseIsoTimestamp("2026-12-01T23:59:00Z"), makeTimestamp(2026, 12, 1, 23, 59));
    EXPECT_EQ(json::parseIsoTimestamp("2026-12-01T08:00:00.000Z"), makeTimestamp(2026, 12, 1, 8, 0));
    EXPECT_EQ(json::parseIsoTimestamp("2026-12-01"), makeTimestamp(2026, 12, 1, 23, 59));
    EXPECT_EQ(json::parseIsoDateStart("2026-12-01"), makeTimestamp(2026, 12, 1, 0, 0));
    EXPECT_FALSE(json::parseIsoTimestamp("2026-02-30"));
    EXPECT_FALSE(json::parseIsoTimestamp("2026-12-01T10:00:00+05:00")) << "only UTC is accepted";
    EXPECT_EQ(json::formatIsoTimestamp(makeTimestamp(2026, 11, 21, 12, 10)), "2026-11-21T12:10:00Z");
    EXPECT_EQ(json::startOfWeek(kNow + 3 * kDay), kNow - 12 * kHour);
}

TEST(Json, PreferencesRoundTripAndReportBadFields) {
    StudyPreferences prefs;
    prefs.studyHours = {{Day::Mon, hm(16), hm(19)}};
    prefs.personalTime = {{Day::Sat, hm(9), hm(12)}};
    std::string error;
    EXPECT_EQ(json::preferencesFromJson(json::toJson(prefs), error), prefs);

    Json::Value bad = json::toJson(prefs);
    bad["studyHours"][0]["day"] = "Funday";
    EXPECT_FALSE(json::preferencesFromJson(bad, error));
    EXPECT_THAT(error, HasSubstr("studyHours"));

    Json::Value wrongType = json::toJson(prefs);
    wrongType["sessionMinutes"] = "sixty";
    EXPECT_FALSE(json::preferencesFromJson(wrongType, error));
    EXPECT_THAT(error, HasSubstr("sessionMinutes"));
}

// ---------------------------------------------------------------- Gemini answers

TEST(Gemini, ParsesScheduleWithOrWithoutCodeFences) {
    const std::string plain =
        R"({"blocks":[{"assignmentId":"t1","title":"Essay","day":"Tue","start":"16:00","end":"17:00"}]})";
    const auto blocks = GeminiClient::parseSchedule(plain);
    ASSERT_THAT(blocks, SizeIs(1));
    EXPECT_EQ(blocks[0], (StudyBlock{"Essay", "t1", {Day::Tue, hm(16), hm(17)}}));
    EXPECT_THAT(GeminiClient::parseSchedule("```json\n" + plain + "\n```"), SizeIs(1));
}

TEST(Gemini, RejectsUnusableAnswers) {
    EXPECT_THROW(GeminiClient::parseSchedule("Sure! Here is your plan."), std::runtime_error);
    EXPECT_THROW(GeminiClient::parseSchedule(R"({"plan":[]})"), std::runtime_error);
    EXPECT_THROW(GeminiClient::parseSchedule(
                     R"({"blocks":[{"assignmentId":"t1","day":"Someday","start":"16:00","end":"17:00"}]})"),
                 std::runtime_error);
}

TEST(Gemini, PromptCarriesPreferencesAndAssignments) {
    StudyPreferences prefs;
    prefs.studyHours = {{Day::Mon, hm(16), hm(19)}};
    const auto prompt = GeminiClient::schedulePrompt(prefs, {{"t1", "Essay", Day::Wed, hm(23, 59), 90}});
    EXPECT_THAT(prompt, HasSubstr(R"("id":"t1")"));
    EXPECT_THAT(prompt, HasSubstr(R"("dueDay":"Wed")"));
    EXPECT_THAT(prompt, HasSubstr(R"("dueTime":"23:59")"));
    EXPECT_THAT(prompt, HasSubstr(R"("sessionMinutes":60)"));
}

TEST(Gemini, SlotsAreSessionsSpacedByBreaksAroundPersonalTime) {
    StudyPreferences prefs;
    prefs.studyHours = {{Day::Tue, hm(10), hm(11)}, {Day::Mon, hm(16), hm(21)}};
    prefs.personalTime = {{Day::Mon, hm(18), hm(18, 30)}};
    prefs.sessionMinutes = 60;
    prefs.breakMinutes = 15;
    EXPECT_EQ(GeminiClient::studySlots(prefs),
              (std::vector<TimeRange>{{Day::Mon, hm(16), hm(17)},
                                      {Day::Mon, hm(17, 15), hm(18)},  // cut short by personal time
                                      {Day::Mon, hm(18, 30), hm(19, 30)},
                                      {Day::Mon, hm(19, 45), hm(20)},  // 60 + 45 + 60 + 15 = the 180 min/day max
                                      {Day::Tue, hm(10), hm(11)}}));
    EXPECT_THAT(GeminiClient::schedulePrompt(prefs, {}),
                HasSubstr(R"("slots":[{"day":"Mon","end":"17:00","start":"16:00"})"));
}

TEST(Gemini, OpenRouterRequestUsesChatCompletionsShape) {
    const auto config = GeminiConfig::openRouter("key");
    EXPECT_EQ(config.host, "https://openrouter.ai");
    EXPECT_EQ(config.model, "anthropic/claude-haiku-5.5");

    const auto body = GeminiClient::requestBody(config, "rules", "plan my week", true);
    EXPECT_EQ(body["model"].asString(), "anthropic/claude-haiku-5.5");
    EXPECT_EQ(body["messages"][0]["role"].asString(), "system");
    EXPECT_EQ(body["messages"][0]["content"].asString(), "rules");
    EXPECT_EQ(body["messages"][1]["role"].asString(), "user");
    EXPECT_EQ(body["messages"][1]["content"].asString(), "plan my week");
    EXPECT_EQ(body["response_format"]["type"].asString(), "json_schema");
    EXPECT_TRUE(body["response_format"]["json_schema"]["strict"].asBool());
    const auto& block = body["response_format"]["json_schema"]["schema"]["properties"]["blocks"]["items"];
    EXPECT_EQ(block["required"].size(), 5u);
    EXPECT_EQ(block["properties"]["day"]["enum"][0].asString(), "Mon");
    EXPECT_EQ(block["properties"]["start"]["pattern"].asString(), "^([01][0-9]|2[0-3]):[0-5][0-9]$");
    EXPECT_FALSE(body.isMember("contents"));
    EXPECT_FALSE(body["reasoning"]["enabled"].asBool());

    EXPECT_FALSE(GeminiClient::requestBody(config, "rules", "hi", false).isMember("response_format"));

    auto reasoning = config;
    reasoning.reasoning = true;
    EXPECT_FALSE(GeminiClient::requestBody(reasoning, "rules", "hi", false).isMember("reasoning"));
}

TEST(Gemini, ReadsTextFromEitherApi) {
    const auto openRouterReply = json::parse(R"({"choices":[{"message":{"content":"hello"}}]})");
    ASSERT_TRUE(openRouterReply);
    EXPECT_EQ(GeminiClient::replyText(GeminiConfig::openRouter("key"), *openRouterReply), "hello");

    const auto geminiReply =
        json::parse(R"({"candidates":[{"content":{"parts":[{"text":"hel"},{"text":"lo"}]}}]})");
    ASSERT_TRUE(geminiReply);
    EXPECT_EQ(GeminiClient::replyText(GeminiConfig{}, *geminiReply), "hello");

    const auto empty = json::parse(R"({"choices":[]})");
    ASSERT_TRUE(empty);
    EXPECT_THROW(GeminiClient::replyText(GeminiConfig::openRouter("key"), *empty), std::runtime_error);
}

// ---------------------------------------------------------------- Agent

class FixedAi : public IAiClient {
public:
    std::vector<StudyBlock> reply;
    bool fail = false;
    int calls = 0;

    std::vector<StudyBlock> proposeSchedule(const StudyPreferences&, const std::vector<Assignment>&) override {
        ++calls;
        if (fail) throw std::runtime_error("Gemini request failed: Timeout");
        return reply;
    }
};

class ThrowingChat : public IChatModel {
public:
    std::string reply(const std::string&, const std::string&) override {
        throw std::runtime_error("network down");
    }
};

class AgentTest : public ApiFixture {
protected:
    std::string alice = addStudent("alice@utd.edu");
    FixedAi gemini;
    AgentController agent{users, taskRepo, settings, schedules, agentRuns, &gemini, nullptr};
};

TEST_F(AgentTest, NeedsPreferencesAndTasks) {
    EXPECT_EQ(agent.generate(alice, std::nullopt, kNow).error().code, ErrorCode::Unprocessable);
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    EXPECT_THAT(agent.generate(alice, std::nullopt, kNow).error().message, HasSubstr("no unfinished tasks"));
}

TEST_F(AgentTest, SavesAValidGeminiSchedule) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto essay = addTask(alice, "Essay", kNow + 2 * kDay, 60);
    gemini.reply = {{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}}};

    const auto plan = agent.generate(alice, std::nullopt, kNow);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    EXPECT_EQ(plan.value().engine, "gemini");
    EXPECT_EQ(schedules.getActiveStudyItems(alice), gemini.reply);
    EXPECT_TRUE(agentRuns.recent(alice, 5).at(0).ok);
}

TEST_F(AgentTest, LabelsPlansWithTheConfiguredModel) {
    AgentController haiku{users, taskRepo, settings, schedules, agentRuns, &gemini, nullptr, "claude-haiku-5.5"};
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto essay = addTask(alice, "Essay", kNow + 2 * kDay, 60);
    gemini.reply = {{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}}};

    const auto plan = haiku.generate(alice, std::nullopt, kNow);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    EXPECT_EQ(plan.value().engine, "claude-haiku-5.5");
    EXPECT_EQ(agentRuns.recent(alice, 5).at(0).engine, "claude-haiku-5.5");
}

TEST_F(AgentTest, NeverSchedulesStudyTimeThatHasPassed) {
    const Timestamp thursday = kNow + 3 * kDay + hm(5);  // Thu 17:00 UTC
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto essay = addTask(alice, "Essay", thursday + kDay, 120);

    // An AI plan using Monday, which is over, is rejected; the local plan starts after now.
    gemini.reply = {{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}}};
    const auto plan = agent.generate(alice, std::nullopt, thursday);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    EXPECT_EQ(plan.value().engine, "local");
    const auto week = planningWeek(thursday, 0);
    for (const auto& block : plan.value().blocks) {
        EXPECT_GE(week.toUtc(block.time.day, block.time.start), thursday) << toString(block.time);
    }
}

TEST_F(AgentTest, SaysSoWhenNoStudyHoursAreLeftThisWeek) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    addTask(alice, "Essay", kNow + 6 * kDay, 60);
    const auto plan = agent.generate(alice, std::nullopt, kNow + 5 * kDay);  // Saturday
    ASSERT_FALSE(plan.ok());
    EXPECT_THAT(plan.error().message, HasSubstr("None of your study hours are left this week"));
}

TEST_F(AgentTest, FallsBackToLocalPlannerWhenGeminiBreaksTheRules) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto essay = addTask(alice, "Essay", kNow + 2 * kDay, 60);
    gemini.reply = {{"Essay", essay.id, {Day::Sat, hm(2), hm(5)}}};  // outside study hours, too long

    const auto plan = agent.generate(alice, std::nullopt, kNow);
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(plan.value().engine, "local");
    EXPECT_THAT(plan.value().warnings, Contains(HasSubstr("outside study hours")));
    EXPECT_EQ(schedules.getActiveStudyItems(alice), plan.value().blocks);

    const auto runs = agentRuns.recent(alice, 5);
    ASSERT_THAT(runs, SizeIs(2));
    EXPECT_THAT(runs, Contains(::testing::Field(&GenerationRun::engine, "gemini")));
    for (const auto& run : runs) {
        EXPECT_EQ(run.ok, run.engine == "local") << "the rejected Gemini answer is recorded, not saved";
    }
}

TEST_F(AgentTest, GeminiOutageDoesNotStopPlanningOrTouchTheSavedSchedule) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    addTask(alice, "Essay", kNow + 2 * kDay, 60);
    gemini.fail = true;
    const auto plan = agent.generate(alice, std::nullopt, kNow);
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(plan.value().engine, "local");
    EXPECT_THAT(plan.value().warnings, Contains(HasSubstr("Timeout")));
}

TEST_F(AgentTest, ImpossiblePlanIsRejectedAndOldScheduleKept) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const std::vector<StudyBlock> old = {{"Old", "x", {Day::Mon, hm(16), hm(17)}}};
    schedules.replace(alice, old);
    addTask(alice, "Due before any study hours", kNow + kHour, 60);  // Mon 13:00
    gemini.fail = true;
    const auto plan = agent.generate(alice, std::nullopt, kNow);
    ASSERT_FALSE(plan.ok());
    EXPECT_EQ(plan.error().code, ErrorCode::Unprocessable);
    EXPECT_FALSE(plan.error().details.empty());
    EXPECT_EQ(schedules.getActiveStudyItems(alice), old);
}

TEST_F(AgentTest, ChatIsUnavailableWithoutGeminiOrWhenItFails) {
    EXPECT_EQ(agent.chat(alice, "What's due?", kNow).error().code, ErrorCode::Unavailable);
    ThrowingChat broken;
    AgentController withChat{users, taskRepo, settings, schedules, agentRuns, nullptr, &broken};
    EXPECT_EQ(withChat.chat(alice, "   ", kNow).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(withChat.chat(alice, "What's due?", kNow).error().code, ErrorCode::Unavailable);
}

// ---------------------------------------------------------------- Schedule

class ScheduleTest : public ApiFixture {
protected:
    std::string alice = addStudent("alice@utd.edu");
    std::string bob = addStudent("bob@utd.edu");
    ScheduleController controller{schedules, settings, taskRepo, users};
    SqlScheduleShareRepository shareRepo{*db};
    RecordingGateway notificationGateway;
    ScheduleSharingController sharing{shareRepo, schedules, users, notificationGateway};
};

TEST_F(ScheduleTest, MoveBlockKeepsPreferencesValid) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto essay = addTask(alice, "Essay", kNow + 3 * kDay, 60);
    schedules.replace(alice, {{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}}});

    const auto bad = controller.moveBlock(
        alice, {essay.id, {Day::Mon, hm(16), hm(17)}, {Day::Sat, hm(10), hm(11)}, ""}, kNow);
    ASSERT_FALSE(bad.ok());
    EXPECT_THAT(bad.error().details, Contains(HasSubstr("outside study hours")));

    const auto good = controller.moveBlock(
        alice, {essay.id, {Day::Mon, hm(16), hm(17)}, {Day::Tue, hm(18), hm(19)}, "Essay draft"}, kNow);
    ASSERT_TRUE(good.ok()) << good.error().message;
    const auto saved = schedules.getActiveStudyItems(alice);
    ASSERT_THAT(saved, SizeIs(1));
    EXPECT_EQ(saved[0].time, (TimeRange{Day::Tue, hm(18), hm(19)}));
    EXPECT_EQ(saved[0].title, "Essay draft");
}

// Generation plans an overdue task as due at Sun 24:00, so its blocks can sit after its
// real due time. Moving some other block must be checked against that same deadline.
TEST_F(ScheduleTest, MoveBlockAllowsOverdueTaskBlocksPlacedAfterTheirRealDueTime) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto lab = addTask(alice, "Lab", kNow + kHour, 60);         // Mon 13:00
    const auto essay = addTask(alice, "Essay", kNow + 3 * kDay, 60);  // Thu 12:00
    const Timestamp later = kNow + 2 * kDay;                          // Wed 12:00: Lab is overdue
    schedules.replace(alice, {{"Lab", lab.id, {Day::Wed, hm(16), hm(17)}},
                              {"Essay", essay.id, {Day::Wed, hm(18), hm(19)}}});

    const auto moved = controller.moveBlock(
        alice, {essay.id, {Day::Wed, hm(18), hm(19)}, {Day::Wed, hm(19), hm(20)}, ""}, later);
    ASSERT_TRUE(moved.ok()) << ::testing::PrintToString(moved.error().details);
    const auto saved = schedules.getActiveStudyItems(alice);
    EXPECT_THAT(saved, Contains(StudyBlock{"Essay", essay.id, {Day::Wed, hm(19), hm(20)}}));
    EXPECT_THAT(saved, Contains(StudyBlock{"Lab", lab.id, {Day::Wed, hm(16), hm(17)}}));
}

TEST_F(ScheduleTest, MoveBlockStillRejectsMovingANotOverdueTaskPastItsDeadline) {
    ASSERT_TRUE(settings.save(alice, weekdayEvenings()));
    const auto essay = addTask(alice, "Essay", kNow + 2 * kDay, 60);  // Wed 12:00
    schedules.replace(alice, {{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}}});

    const auto late = controller.moveBlock(
        alice, {essay.id, {Day::Mon, hm(16), hm(17)}, {Day::Thu, hm(16), hm(17)}, ""}, kNow);
    ASSERT_FALSE(late.ok());
    EXPECT_EQ(late.error().code, ErrorCode::Unprocessable);
    EXPECT_THAT(late.error().details, Contains(HasSubstr("ends after \"Essay\" is due")));
    EXPECT_EQ(schedules.getActiveStudyItems(alice),
              (std::vector<StudyBlock>{{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}}}));
}

TEST_F(ScheduleTest, RemoveBlockDeletesOnlyTheMatchingBlock) {
    const auto essay = addTask(alice, "Essay", kNow + 3 * kDay, 120);
    const StudyBlock first{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}};
    const StudyBlock second{"Essay", essay.id, {Day::Thu, hm(10) + 30, hm(12)}};
    schedules.replace(alice, {first, second});

    const auto removed = controller.removeBlock(alice, {essay.id, second.time});
    ASSERT_TRUE(removed.ok()) << removed.error().message;
    EXPECT_EQ(removed.value(), std::vector<StudyBlock>{first});
    EXPECT_EQ(schedules.getActiveStudyItems(alice), std::vector<StudyBlock>{first});
}

TEST_F(ScheduleTest, RemoveBlockIs404WhenNothingMatches) {
    const auto essay = addTask(alice, "Essay", kNow + 3 * kDay, 60);
    const StudyBlock block{"Essay", essay.id, {Day::Mon, hm(16), hm(17)}};
    schedules.replace(alice, {block});

    for (const BlockRef& ref : {BlockRef{essay.id, {Day::Mon, hm(16), hm(18)}},
                                BlockRef{essay.id, {Day::Tue, hm(16), hm(17)}},
                                BlockRef{"other-task", block.time}}) {
        const auto removed = controller.removeBlock(alice, ref);
        ASSERT_FALSE(removed.ok());
        EXPECT_EQ(removed.error().code, ErrorCode::NotFound);
        EXPECT_EQ(removed.error().message, "That block isn't on your schedule");
    }
    EXPECT_EQ(schedules.getActiveStudyItems(alice), std::vector<StudyBlock>{block});
}

TEST_F(ScheduleTest, RemoveBlockCannotTouchAnotherStudentsSchedule) {
    const auto bobsTask = addTask(bob, "Lab", kNow + 3 * kDay, 60);
    const StudyBlock bobsBlock{"Lab", bobsTask.id, {Day::Wed, hm(18), hm(19)}};
    schedules.replace(bob, {bobsBlock});

    const auto removed = controller.removeBlock(alice, {bobsTask.id, bobsBlock.time});
    ASSERT_FALSE(removed.ok());
    EXPECT_EQ(removed.error().code, ErrorCode::NotFound);
    EXPECT_EQ(schedules.getActiveStudyItems(bob), std::vector<StudyBlock>{bobsBlock});
}

TEST_F(ScheduleTest, ExportsICalendar) {
    const auto essay = addTask(alice, "Essay, final; v2", kNow + 3 * kDay, 60);
    schedules.replace(alice, {{"Essay", essay.id, {Day::Tue, hm(18), hm(19)}}});
    const auto ics = controller.exportIcs(alice, kNow);
    ASSERT_TRUE(ics.ok());
    EXPECT_THAT(ics.value(), HasSubstr("BEGIN:VCALENDAR\r\n"));
    EXPECT_THAT(ics.value(), HasSubstr("DTSTART:20261124T180000Z\r\n"));
    EXPECT_THAT(ics.value(), HasSubstr("SUMMARY:Due: Essay\\, final\\; v2\r\n"));
    EXPECT_THAT(ics.value(), HasSubstr("END:VCALENDAR\r\n"));
}

TEST_F(ScheduleTest, ExportsAnEmptyScheduleAsAnEmptyCalendar) {
    const auto ics = controller.exportIcs(alice, kNow);
    ASSERT_TRUE(ics.ok()) << ics.error().message;
    EXPECT_THAT(ics.value(), HasSubstr("BEGIN:VCALENDAR\r\n"));
    EXPECT_THAT(ics.value(), HasSubstr("END:VCALENDAR\r\n"));
    EXPECT_THAT(ics.value(), ::testing::Not(HasSubstr("BEGIN:VEVENT")));
}

TEST_F(ScheduleTest, SharesScheduleWithRegisteredRecipientAndReflectsOwnerChanges) {
    schedules.replace(alice, {{"Essay", "task-1", {Day::Mon, hm(16), hm(17)}}});

    const auto created = sharing.shareWith(alice, " BOB@UTD.EDU ", kNow);
    ASSERT_TRUE(created.ok()) << created.error().message;
    EXPECT_EQ(created.value().recipientId, bob);
    ASSERT_THAT(notificationGateway.sent, SizeIs(1));
    EXPECT_EQ(notificationGateway.sent[0].studentId, bob);

    const auto view = sharing.viewShared(bob, created.value().id);
    ASSERT_TRUE(view.ok()) << view.error().message;
    ASSERT_THAT(view.value().blocks, SizeIs(1));
    EXPECT_EQ(view.value().blocks[0].title, "Essay");

    schedules.replace(alice, {{"Lab", "task-2", {Day::Tue, hm(18), hm(19)}}});
    const auto updated = sharing.viewShared(bob, created.value().id);
    ASSERT_TRUE(updated.ok());
    ASSERT_THAT(updated.value().blocks, SizeIs(1));
    EXPECT_EQ(updated.value().blocks[0].title, "Lab");
}

TEST_F(ScheduleTest, SharedScheduleIsPrivateToRecipientAndCanBeRevoked) {
    const auto created = sharing.shareWith(alice, "bob@utd.edu", kNow);
    ASSERT_TRUE(created.ok());

    EXPECT_FALSE(sharing.viewShared(alice, created.value().id).ok());
    EXPECT_FALSE(sharing.viewShared(bob, "not-a-share").ok());
    ASSERT_TRUE(sharing.revoke(alice, created.value().id).ok());
    EXPECT_FALSE(sharing.viewShared(bob, created.value().id).ok());
}

TEST_F(ScheduleTest, RejectsInvalidRecipientsAndDoesNotDuplicateSharesOrNotifications) {
    EXPECT_FALSE(sharing.shareWith(alice, "not-an-email", kNow).ok());
    EXPECT_FALSE(sharing.shareWith(alice, "unknown@utd.edu", kNow).ok());
    EXPECT_FALSE(sharing.shareWith(alice, "alice@utd.edu", kNow).ok());

    ASSERT_TRUE(sharing.shareWith(alice, "bob@utd.edu", kNow).ok());
    ASSERT_TRUE(sharing.shareWith(alice, "BOB@UTD.EDU", kNow + 1).ok());
    EXPECT_THAT(sharing.listOwnedShares(alice), SizeIs(1));
    EXPECT_THAT(sharing.listReceivedShares(bob), SizeIs(1));
    EXPECT_THAT(notificationGateway.sent, SizeIs(1));
}

TEST_F(ScheduleTest, DashboardShowsTodayUpcomingAndOverdue) {
    addTask(alice, "Low priority", kNow + kDay, 60, 3);
    addTask(alice, "High priority", kNow + 2 * kDay, 60, 1);
    addTask(alice, "Overdue", kNow - kDay);
    addTask(alice, "Far away", kNow + 30 * kDay);
    schedules.replace(alice, {{"High priority", "x", {Day::Mon, hm(16), hm(17)}},
                              {"Low priority", "y", {Day::Tue, hm(16), hm(17)}}});

    const auto d = controller.dashboard(alice, kNow).value();
    EXPECT_EQ(d.today, Day::Mon);
    EXPECT_THAT(d.todaysBlocks, SizeIs(1));
    ASSERT_THAT(d.upcoming, SizeIs(2));
    EXPECT_EQ(d.upcoming[0].title, "High priority");
    ASSERT_THAT(d.overdue, SizeIs(1));
    EXPECT_EQ(d.notStarted, 4);
    EXPECT_FALSE(d.hasPreferences);
}

}  // namespace
}  // namespace cramberry
