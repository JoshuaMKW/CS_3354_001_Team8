// UC-5 fallback planner and the task -> weekly-plan mapping (NFR-15).

#include <gmock/gmock.h>

#include "agent/Planning.h"
#include "schedule/NKScheduleRules.h"
#include "tests/ApiFixture.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;
using ::testing::IsEmpty;

StudyPreferences evenings() {
    StudyPreferences prefs;
    for (Day d : {Day::Mon, Day::Tue, Day::Wed, Day::Thu, Day::Fri}) {
        prefs.studyHours.push_back({d, hm(16), hm(21)});
    }
    prefs.maxStudyMinutesPerDay = 180;
    prefs.sessionMinutes = 60;
    prefs.breakMinutes = 15;
    prefs.personalTime = {{Day::Tue, hm(18), hm(19)}};
    return prefs;
}

TEST(LocalPlanner, ScheduleFollowsEveryPreference) {
    const auto prefs = evenings();
    const std::vector<Assignment> work = {
        {"a1", "CS 3354 assignment", Day::Wed, hm(23, 59), 150},
        {"a2", "Algorithms ch. 6", Day::Fri, hm(12), 120},
        {"a3", "Linear algebra notes", Day::Sun, kMinutesPerDay, 90},
    };
    LocalPlanner planner;
    const auto blocks = planner.proposeSchedule(prefs, work);
    EXPECT_THAT(validateNKSchedule(blocks, prefs, work), IsEmpty());
    EXPECT_THAT(planner.shortfalls(), IsEmpty());

    std::map<std::string, int> minutes;
    for (const auto& b : blocks) minutes[b.assignmentId] += b.time.length();
    EXPECT_EQ(minutes["a1"], 150);
    EXPECT_EQ(minutes["a2"], 120);
    EXPECT_EQ(minutes["a3"], 90);
}

TEST(LocalPlanner, EarliestDeadlineGoesFirst) {
    const auto prefs = evenings();
    const std::vector<Assignment> work = {
        {"later", "Later", Day::Fri, hm(23), 60},
        {"sooner", "Sooner", Day::Mon, hm(23), 60},
    };
    LocalPlanner planner;
    const auto blocks = planner.proposeSchedule(prefs, work);
    ASSERT_FALSE(blocks.empty());
    EXPECT_EQ(blocks.front().assignmentId, "sooner");
    EXPECT_EQ(blocks.front().time, (TimeRange{Day::Mon, hm(16), hm(17)}));
}

TEST(LocalPlanner, ReportsWorkThatDoesNotFitAndAgentRejectsUnscheduledTasks) {
    StudyPreferences prefs = evenings();
    prefs.studyHours = {{Day::Mon, hm(16), hm(17)}};
    const std::vector<Assignment> work = {
        {"big", "Big project", Day::Mon, hm(23), 240},
        {"none", "No room", Day::Mon, hm(12), 60},  // due before any study hours
    };
    LocalPlanner planner;
    Agent agent(planner);
    const auto result = agent.generateStudySchedule(prefs, work);
    EXPECT_FALSE(result.ok);
    EXPECT_THAT(result.violations, ::testing::Contains(::testing::HasSubstr("\"No room\" is not scheduled")));
    EXPECT_THAT(planner.shortfalls(), ::testing::Contains(::testing::HasSubstr("Only 60 of 240 min")));
}

TEST(Planning, WeekIsTheStudentsLocalMonday) {
    // 2026-11-23 03:00 UTC is still Sunday 21:00 in UTC-6.
    const Timestamp now = makeTimestamp(2026, 11, 23, 3, 0);
    const auto week = planningWeek(now, -360);
    EXPECT_EQ(week.startUtc, makeTimestamp(2026, 11, 16, 6, 0));
    const auto local = week.toLocal(now);
    ASSERT_TRUE(local);
    EXPECT_EQ(local->first, Day::Sun);
    EXPECT_EQ(local->second, hm(21));
    EXPECT_EQ(week.toUtc(Day::Mon, hm(16)), makeTimestamp(2026, 11, 16, 22, 0));
}

TEST(Planning, PlannerAssignmentsCoverOverdueAndTheNextSevenDays) {
    const auto week = planningWeek(kNow, 0);
    auto task = [](std::string id, Timestamp due, TaskStatus status = TaskStatus::NotStarted) {
        Task t;
        t.id = id;
        t.title = id;
        t.dueAt = due;
        t.status = status;
        return t;
    };
    const std::vector<Task> tasks = {
        task("wed", makeTimestamp(2026, 11, 25, 17, 30)),
        task("overdue", kNow - kDay),
        task("next-mon", makeTimestamp(2026, 11, 30, 1, 0)),  // after this week, within 7 days
        task("far", kNow + 10 * kDay),
        task("done", kNow + kDay, TaskStatus::Completed),
    };
    const auto assignments = plannerAssignments(tasks, week, kNow);
    ASSERT_EQ(assignments.size(), 3u);
    EXPECT_EQ(assignments[0].id, "wed");
    EXPECT_EQ(assignments[0].dueDay, Day::Wed);
    EXPECT_EQ(assignments[0].dueMinute, hm(17, 30));
    EXPECT_EQ(assignments[1].id, "overdue");
    EXPECT_EQ(assignments[1].dueDay, Day::Sun);
    EXPECT_EQ(assignments[1].dueMinute, kMinutesPerDay);
    EXPECT_EQ(assignments[2].id, "next-mon");
    EXPECT_EQ(assignments[2].dueDay, Day::Sun);
    EXPECT_EQ(assignments[2].dueMinute, kMinutesPerDay);
}

TEST(Planning, RemainingStudyHoursDropThePast) {
    const Timestamp now = makeTimestamp(2026, 11, 25, 17, 7);  // Wed 17:07 UTC
    const auto week = planningWeek(now, 0);
    StudyPreferences prefs;
    prefs.studyHours = {{Day::Mon, hm(16), hm(21)},      // earlier day: dropped
                        {Day::Wed, hm(9), hm(12)},       // earlier today: dropped
                        {Day::Wed, hm(16), hm(18)},      // in progress: starts at 17:10
                        {Day::Wed, hm(17), hm(17, 20)},  // only 10 min left: dropped
                        {Day::Thu, hm(16), hm(21)}};     // later: kept
    prefs.personalTime = {{Day::Mon, hm(18), hm(19)}};
    prefs.sessionMinutes = 45;

    const auto remaining = remainingStudyHours(prefs, week, now);
    EXPECT_EQ(remaining.studyHours,
              (std::vector<TimeRange>{{Day::Wed, hm(17, 10), hm(18)}, {Day::Thu, hm(16), hm(21)}}));
    EXPECT_EQ(remaining.personalTime, prefs.personalTime);
    EXPECT_EQ(remaining.sessionMinutes, 45);
}

}  // namespace
}  // namespace cramberry
