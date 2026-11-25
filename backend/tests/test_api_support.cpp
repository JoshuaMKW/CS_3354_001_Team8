// JSON shapes, Gemini answer handling (UC-12, FR-29), AI fallback (NFR-15) and the
// schedule features built on top: moving blocks (UC-6), export (UC-8), dashboard.

#include <gmock/gmock.h>

#include <stdexcept>

#include "agent/AgentController.h"
#include "agent/GeminiClient.h"
#include "common/Json.h"
#include "schedule/ScheduleController.h"
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
    ScheduleController controller{schedules, settings, taskRepo, users};
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
