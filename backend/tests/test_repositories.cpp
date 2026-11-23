// /data-api and the SQL repositories: round trips, atomic writes (NFR-19) and the
// reminder pipeline from the tasks table to the in-app inbox (UC-16).

#include <gmock/gmock.h>

#include <stdexcept>

#include "notifications/ReminderScheduler.h"
#include "tests/ApiFixture.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;
using ::testing::ElementsAre;
using ::testing::SizeIs;

class Repositories : public ApiFixture {
protected:
    std::string alice = addStudent("alice@utd.edu");
    std::string bob = addStudent("bob@utd.edu");
};

TEST(Database, TranslatesPlaceholdersForSqlite) {
    const auto db = data::Database::openInMemory();
    EXPECT_EQ(db->translate("SELECT $1, $2 WHERE x = $10"), "SELECT ?1, ?2 WHERE x = ?10");
    EXPECT_EQ(db->translate("SELECT '$' || name"), "SELECT '$' || name");
    EXPECT_TRUE(db->ping());
}

TEST_F(Repositories, StudyPreferencesRoundTrip) {
    EXPECT_FALSE(settings.find(alice));
    auto prefs = weekdayEvenings();
    prefs.personalTime = {{Day::Sat, hm(9), hm(12)}};
    ASSERT_TRUE(settings.save(alice, prefs));
    EXPECT_EQ(settings.find(alice), prefs);
    prefs.sessionMinutes = 45;
    ASSERT_TRUE(settings.save(alice, prefs));  // upsert
    EXPECT_EQ(settings.find(alice)->sessionMinutes, 45);
    EXPECT_FALSE(settings.find(bob));
}

TEST_F(Repositories, NotificationSettingsRoundTrip) {
    NotificationSettings s;
    s.enabled = true;
    s.email = true;
    s.sms = true;
    s.phoneNumber = "+1 972-555-0100";
    s.deadlineReminders = true;
    s.deadlineLeadMinutes = 120;
    ASSERT_TRUE(notificationSettings.save(alice, s));
    EXPECT_EQ(notificationSettings.find(alice), s);
    EXPECT_FALSE(notificationSettings.find(bob));
}

TEST_F(Repositories, ScheduleReplaceSwapsTheWholeScheduleForOneStudentOnly) {
    const StudyBlock a1{"Essay", "t1", {Day::Mon, hm(16), hm(17)}};
    const StudyBlock a2{"Lab", "t2", {Day::Tue, hm(18), hm(19)}};
    const StudyBlock b1{"Bob's", "t3", {Day::Wed, hm(10), hm(11)}};
    schedules.replace(alice, {a2, a1});
    schedules.replace(bob, {b1});
    EXPECT_THAT(schedules.getActiveStudyItems(alice), ElementsAre(a1, a2));  // ordered by day/time

    schedules.replace(alice, {a2});
    EXPECT_THAT(schedules.getActiveStudyItems(alice), ElementsAre(a2));
    EXPECT_THAT(schedules.getActiveStudyItems(bob), ElementsAre(b1));
}

TEST_F(Repositories, FailedTransactionLeavesDataUnchanged) {
    schedules.replace(alice, {{"Essay", "t1", {Day::Mon, hm(16), hm(17)}}});
    {
        auto tx = db->begin();
        tx.exec("DELETE FROM study_blocks WHERE student_id = $1", alice);
        EXPECT_ANY_THROW(tx.exec("INSERT INTO no_such_table VALUES (1)"));
        EXPECT_THROW(tx.commit(), std::runtime_error);
    }
    EXPECT_THAT(schedules.getActiveStudyItems(alice), SizeIs(1));

    {
        auto tx = db->begin();
        tx.exec("DELETE FROM study_blocks WHERE student_id = $1", alice);
        // Dropped without commit(): rolled back.
    }
    EXPECT_THAT(schedules.getActiveStudyItems(alice), SizeIs(1));
}

TEST_F(Repositories, InboxIsPerStudent) {
    const auto id = inbox.add({alice, Channel::Desktop, "Reminder", "Essay is due", kNow});
    inbox.add({alice, Channel::Email, "Reminder 2", "Lab is due", kNow + 1});
    EXPECT_THAT(inbox.list(alice, true, 10), SizeIs(2));
    EXPECT_THAT(inbox.list(bob, false, 10), SizeIs(0));
    EXPECT_FALSE(inbox.markRead(bob, id));
    EXPECT_TRUE(inbox.markRead(alice, id));
    const auto unread = inbox.list(alice, true, 10);
    ASSERT_THAT(unread, SizeIs(1));
    EXPECT_EQ(unread[0].subject, "Reminder 2");  // newest first
    inbox.markAllRead(alice);
    EXPECT_THAT(inbox.list(alice, true, 10), SizeIs(0));
}

TEST_F(Repositories, GenerationRunsAreRecorded) {
    agentRuns.record({"r1", alice, kNow, "gemini", false, 0, {"\"Essay\" is not scheduled"}});
    agentRuns.record({"r2", alice, kNow + 1, "local", true, 3, {}});
    const auto runs = agentRuns.recent(alice, 10);
    ASSERT_THAT(runs, SizeIs(2));
    EXPECT_EQ(runs[0].engine, "local");
    EXPECT_TRUE(runs[0].ok);
    EXPECT_THAT(runs[1].details, ElementsAre("\"Essay\" is not scheduled"));
    EXPECT_THAT(agentRuns.recent(bob, 10), SizeIs(0));
}

// UC-16 end to end: a task due tomorrow -> one reminder per channel, sent once.
TEST_F(Repositories, ReminderSchedulerDeliversDueTasksToTheInbox) {
    const auto essay = addTask(alice, "Essay", kNow + kDay);
    addTask(bob, "Bob's essay", kNow + kDay);  // Bob has notifications off
    NotificationSettings on;
    on.enabled = true;
    on.desktop = true;
    on.email = true;
    on.deadlineReminders = true;
    on.deadlineLeadMinutes = kDay;
    ASSERT_TRUE(notificationSettings.save(alice, on));

    SqlDeadlineSource deadlines(*db);
    InboxGateway gateway(inbox);
    ReminderScheduler scheduler(notificationSettings, deadlines, gateway);
    scheduler.tick(kNow);
    scheduler.tick(kNow + 1);

    const auto items = inbox.list(alice, false, 10);
    ASSERT_THAT(items, SizeIs(2));
    EXPECT_EQ(items[0].subject, "Reminder: Essay is due soon");
    EXPECT_THAT(inbox.list(bob, false, 10), SizeIs(0));

    ASSERT_TRUE(tasks.setStatus(alice, essay.id, TaskStatus::Completed, kNow).ok());
    EXPECT_THAT(deadlines.upcomingDeadlines(kNow), SizeIs(1)) << "completed tasks get no reminders";
}

}  // namespace
}  // namespace cramberry
