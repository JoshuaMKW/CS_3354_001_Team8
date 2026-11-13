// UC-15 Manage Study Preferences: TC-15-01 to TC-15-04.
// Test author: Joshua Alston (JA). See docs/testing/JA_Test_Cases.md.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <map>

#include "agent/Agent.h"
#include "settings/SettingsController.h"
#include "tests/Fakes.h"

namespace cramberry {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using namespace cramberry::testing;

const std::string kStudent = "student-a";
const std::string kOtherStudent = "student-b";

StudyPreferences defaultPreferences() {
    StudyPreferences prefs;
    for (Day d : {Day::Mon, Day::Tue, Day::Wed, Day::Thu, Day::Fri}) {
        prefs.studyHours.push_back({d, hm(16), hm(21)});
    }
    prefs.maxStudyMinutesPerDay = 180;
    prefs.sessionMinutes = 90;
    prefs.breakMinutes = 15;
    return prefs;
}

class ManageStudyPreferences : public ::testing::Test {
protected:
    InMemorySettingsRepository settings;
    InMemoryScheduleRepository schedule;
    RecordingRegenerator regenerator;
    SettingsController controller{settings, schedule, regenerator};

    void SetUp() override {
        settings.rows[kStudent] = defaultPreferences();
        StudyPreferences other = defaultPreferences();
        other.sessionMinutes = 45;
        settings.rows[kOtherStudent] = other;
        settings.writes = 0;
    }
};

// TC-15-01 | Save Valid Study Preferences (Successful Flow)
// FR-7, NFR-2: valid preferences are saved and kept after a reload.
TEST_F(ManageStudyPreferences, TC_15_01_SaveValidStudyPreferences) {
    StudyPreferences prefs;
    prefs.studyHours = {{Day::Mon, hm(16), hm(19)},
                        {Day::Wed, hm(16), hm(19)},
                        {Day::Fri, hm(16), hm(19)}};
    prefs.maxStudyMinutesPerDay = 180;
    prefs.sessionMinutes = 60;
    prefs.breakMinutes = 15;
    prefs.personalTime = {{Day::Sun, hm(18), hm(19)}};
    const StudyPreferences otherBefore = settings.rows.at(kOtherStudent);

    const SaveResult result = controller.saveStudyPreferences(kStudent, prefs);

    EXPECT_EQ(result.status, SaveResult::Status::Saved);
    EXPECT_EQ(result.message, "Settings saved.");
    // "Reload the page": read the row back from storage.
    ASSERT_TRUE(settings.find(kStudent).has_value());
    EXPECT_EQ(*settings.find(kStudent), prefs);
    // Only this student's row changed.
    EXPECT_EQ(settings.writes, 1);
    EXPECT_EQ(settings.rows.at(kOtherStudent), otherBefore);
    // The AI Scheduling System is asked to rebuild this student's schedule once.
    EXPECT_EQ(regenerator.calls, std::vector<std::string>{kStudent});
}

// TC-15-02 | Personal Time Conflict (Alternative Flow)
// Sequence diagram: CheckForConflicts() -> [exception] Show Conflict.
TEST_F(ManageStudyPreferences, TC_15_02_PersonalTimeConflictIsFlaggedNotOverwritten) {
    const StudyBlock review{"CS 3354 Review", "cs3354-review", {Day::Tue, hm(18), hm(19, 30)}};
    schedule.schedules[kStudent] = {review};
    const StudyPreferences before = settings.rows.at(kStudent);

    StudyPreferences prefs = before;
    prefs.personalTime.push_back({Day::Tue, hm(18), hm(20)});

    const SaveResult result = controller.saveStudyPreferences(kStudent, prefs);

    EXPECT_EQ(result.status, SaveResult::Status::Conflict);
    EXPECT_THAT(result.message, HasSubstr("CS 3354 Review"));
    EXPECT_THAT(result.message, HasSubstr("Tue 6:00 PM-7:30 PM"));
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0], review);
    // No schedule regenerated, block unchanged, preferences not overwritten.
    EXPECT_THAT(regenerator.calls, IsEmpty());
    EXPECT_EQ(schedule.getActiveStudyItems(kStudent), std::vector<StudyBlock>{review});
    EXPECT_EQ(settings.writes, 0);
    EXPECT_EQ(*settings.find(kStudent), before);
}

// TC-15-03 | Break Longer Than Session (Invalid Input)
// Validate Data -> extends Show Error. Error text matches the Study Planner Settings wireframe.
TEST_F(ManageStudyPreferences, TC_15_03_BreakLongerThanSessionIsRejected) {
    const StudyPreferences before = settings.rows.at(kStudent);
    ASSERT_EQ(before.sessionMinutes, 90);
    ASSERT_EQ(before.breakMinutes, 15);

    StudyPreferences prefs = before;
    prefs.breakMinutes = 120;

    const SaveResult result = controller.saveStudyPreferences(kStudent, prefs);

    EXPECT_EQ(result.status, SaveResult::Status::InvalidInput);
    EXPECT_EQ(result.message, "Break can't be longer than your 90 min session");
    EXPECT_EQ(settings.writes, 0);
    EXPECT_EQ(settings.find(kStudent)->breakMinutes, 15);
    EXPECT_THAT(regenerator.calls, IsEmpty());
}

// --- TC-15-04 helpers: check the schedule directly, independent of the Agent's own checks. ---

int weekMinute(Day day, int minute) { return static_cast<int>(day) * kMinutesPerDay + minute; }

void expectScheduleFollowsPreferences(const std::vector<StudyBlock>& blocks,
                                      const StudyPreferences& prefs,
                                      const std::vector<Assignment>& assignments) {
    std::map<Day, int> minutesPerDay;
    std::map<Day, std::vector<TimeRange>> perDay;
    for (const auto& b : blocks) {
        SCOPED_TRACE(b.title + " " + toString(b.time));
        EXPECT_LE(static_cast<int>(b.time.day), static_cast<int>(Day::Fri)) << "not Mon-Fri";
        EXPECT_GE(b.time.start, hm(16));
        EXPECT_LE(b.time.end, hm(19));
        for (const auto& personal : prefs.personalTime) {
            EXPECT_FALSE(b.time.overlaps(personal)) << "overlaps " << toString(personal);
        }
        EXPECT_LE(b.time.length(), 60);
        minutesPerDay[b.time.day] += b.time.length();
        perDay[b.time.day].push_back(b.time);
    }
    for (const auto& [day, total] : minutesPerDay) {
        EXPECT_LE(total, 120) << dayName(day);
    }
    for (auto& [day, ranges] : perDay) {
        std::sort(ranges.begin(), ranges.end(),
                  [](const TimeRange& a, const TimeRange& b) { return a.start < b.start; });
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            EXPECT_GE(ranges[i].start - ranges[i - 1].end, 15) << toString(ranges[i]);
        }
    }
    for (const auto& a : assignments) {
        int scheduled = 0;
        for (const auto& b : blocks) {
            if (b.assignmentId != a.id) continue;
            ++scheduled;
            EXPECT_LE(weekMinute(b.time.day, b.time.end), weekMinute(a.dueDay, a.dueMinute))
                << b.title << " ends after " << a.title << " is due";
        }
        EXPECT_GT(scheduled, 0) << a.title << " is not scheduled";
    }
}

class ScheduleFollowsPreferences : public ::testing::Test {
protected:
    StudyPreferences prefs;
    std::vector<Assignment> assignments;

    void SetUp() override {
        for (Day d : {Day::Mon, Day::Tue, Day::Wed, Day::Thu, Day::Fri}) {
            prefs.studyHours.push_back({d, hm(16), hm(19)});
        }
        prefs.maxStudyMinutesPerDay = 120;
        prefs.sessionMinutes = 60;
        prefs.breakMinutes = 15;
        prefs.personalTime = {{Day::Wed, hm(17), hm(18)}};
        assignments = {
            {"cs3354-project", "CS 3354 Project", Day::Thu, hm(23, 59), 180},
            {"math2414-hw", "MATH 2414 HW", Day::Wed, hm(23, 59), 120},
            {"cs4347-review", "CS 4347 Exam Review", Day::Fri, hm(12), 120},
        };
    }
};

// TC-15-04 | Schedule Follows Preferences (Integration)
// FR-16, UC-15: the AI-generated schedule respects the saved study preferences.
TEST_F(ScheduleFollowsPreferences, TC_15_04_GeneratedScheduleFollowsPreferences) {
    CannedAiClient gemini({
        {"CS 3354 Project", "cs3354-project", {Day::Mon, hm(16), hm(17)}},
        {"MATH 2414 HW", "math2414-hw", {Day::Mon, hm(17, 15), hm(18, 15)}},
        {"CS 3354 Project", "cs3354-project", {Day::Tue, hm(16), hm(17)}},
        {"CS 4347 Exam Review", "cs4347-review", {Day::Tue, hm(17, 15), hm(18, 15)}},
        {"MATH 2414 HW", "math2414-hw", {Day::Wed, hm(16), hm(17)}},
        {"CS 4347 Exam Review", "cs4347-review", {Day::Wed, hm(18), hm(19)}},
        {"CS 3354 Project", "cs3354-project", {Day::Thu, hm(16), hm(17)}},
    });
    Agent agent(gemini);

    const GenerateResult result = agent.generateStudySchedule(prefs, assignments);

    ASSERT_TRUE(result.ok) << ::testing::PrintToString(result.violations);
    EXPECT_EQ(result.blocks.size(), 7u);
    expectScheduleFollowsPreferences(result.blocks, prefs, assignments);
}

// TC-15-04 (negative check): if Gemini proposes a block during personal time,
// the Agent must reject the draft instead of showing it to the student.
TEST_F(ScheduleFollowsPreferences, TC_15_04_ScheduleThatBreaksPreferencesIsRejected) {
    CannedAiClient gemini({
        {"CS 3354 Project", "cs3354-project", {Day::Mon, hm(16), hm(17)}},
        {"MATH 2414 HW", "math2414-hw", {Day::Wed, hm(17), hm(18)}},  // personal time
        {"CS 4347 Exam Review", "cs4347-review", {Day::Tue, hm(16), hm(17)}},
    });
    Agent agent(gemini);

    const GenerateResult result = agent.generateStudySchedule(prefs, assignments);

    EXPECT_FALSE(result.ok);
    EXPECT_THAT(result.blocks, IsEmpty());
    ASSERT_THAT(result.violations, Not(IsEmpty()));
    EXPECT_THAT(result.violations[0], HasSubstr("personal time"));
}

}  // namespace
}  // namespace cramberry
