// UC-16 Update Notification Settings: TC-16-01 to TC-16-04.
// Test author: Joshua Alston (JA). See docs/testing/JA_Test_Cases.md.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "notifications/NotificationController.h"
#include "notifications/ReminderScheduler.h"
#include "tests/Fakes.h"

namespace cramberry {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using namespace cramberry::testing;

const std::string kStudent = "student-a";
constexpr int kOneDay = 24 * 60;

// Fixed "now" so the tests never depend on the real clock: 2026-11-20 12:00 UTC.
const Timestamp kT0 = makeTimestamp(2026, 11, 20, 12, 0);

NotificationSettings emailDeadlineReminders() {
    NotificationSettings s;
    s.enabled = true;
    s.email = true;
    s.deadlineReminders = true;
    s.deadlineLeadMinutes = kOneDay;
    return s;
}

class NotificationSettingsTest : public ::testing::Test {
protected:
    InMemoryNotificationRepository repository;
    NotificationController controller{repository};
    FakeTaskSource tasks;
    RecordingGateway inbox;
    ReminderScheduler scheduler{repository, tasks, inbox};

    // Run the scheduler once a minute over [from, to].
    void runScheduler(Timestamp from, Timestamp to) {
        for (Timestamp now = from; now <= to; ++now) scheduler.tick(now);
    }
};

// TC-16-01 | Opt In to Email Reminders (Successful Flow)
// FR-12: opt-in email notification system.
TEST_F(NotificationSettingsTest, TC_16_01_OptInToEmailReminders) {
    repository.rows[kStudent] = NotificationSettings{};  // notifications off
    repository.writes = 0;
    const NotificationSettings settings = emailDeadlineReminders();

    const UpdateResult result = controller.updateSettings(kStudent, settings);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.message, "Notification settings saved.");
    // "Reload the page": read the row back from storage.
    ASSERT_TRUE(repository.find(kStudent).has_value());
    const NotificationSettings saved = *repository.find(kStudent);
    EXPECT_TRUE(saved.enabled);
    EXPECT_TRUE(saved.email);
    EXPECT_TRUE(saved.deadlineReminders);
    EXPECT_EQ(saved.deadlineLeadMinutes, kOneDay);
    EXPECT_EQ(saved, settings);
    EXPECT_EQ(repository.writes, 1);
}

// TC-16-02 | Opt Out of All Notifications (Alternative Flow)
// FR-13: the student can remove themselves from the notification system.
TEST_F(NotificationSettingsTest, TC_16_02_OptOutStopsAllReminders) {
    NotificationSettings on = emailDeadlineReminders();
    on.desktop = true;
    repository.rows[kStudent] = on;
    tasks.items = {{kStudent, "hw2", "Test HW 2", kT0 + kOneDay + 10}};

    NotificationSettings off = on;
    off.enabled = false;
    const UpdateResult result = controller.updateSettings(kStudent, off);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.message, "Notification settings saved.");
    EXPECT_FALSE(repository.find(kStudent)->enabled);

    // Wait until 30 minutes past when the reminder would have been sent (kT0 + 10).
    runScheduler(kT0, kT0 + 40);
    EXPECT_THAT(inbox.sent, IsEmpty()) << "no email or desktop reminder for Test HW 2";
}

// TC-16-03 | Invalid Phone Number (Invalid Input)
// ValidatePreferences() -> [exception] Show Error; nothing is saved.
TEST_F(NotificationSettingsTest, TC_16_03_InvalidPhoneNumberIsRejected) {
    const NotificationSettings before = emailDeadlineReminders();  // Email is the only source
    repository.rows[kStudent] = before;
    repository.writes = 0;

    NotificationSettings withSms = before;
    withSms.sms = true;
    withSms.phoneNumber = "12345";

    const UpdateResult result = controller.updateSettings(kStudent, withSms);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Enter a valid phone number");
    EXPECT_EQ(repository.writes, 0);
    const NotificationSettings stored = *repository.find(kStudent);
    EXPECT_FALSE(stored.sms);
    EXPECT_TRUE(stored.phoneNumber.empty());
    EXPECT_EQ(stored, before);
}

// TC-16-04 | Reminder Sent on Time (Integration)
// UC-16: timed alert for an upcoming deadline, sent once at the chosen lead time.
TEST_F(NotificationSettingsTest, TC_16_04_DeadlineReminderIsSentOnceOnTime) {
    ASSERT_TRUE(controller.updateSettings(kStudent, emailDeadlineReminders()).ok);  // TC-16-01 setup
    const Timestamp due = kT0 + kOneDay + 10;  // "Test HW 1" due in 24 h 10 min
    tasks.items = {{kStudent, "hw1", "Test HW 1", due}};

    runScheduler(kT0, kT0 + 15);  // wait 15 min, check inbox
    ASSERT_EQ(inbox.sent.size(), 1u);
    const OutgoingMessage& email = inbox.sent[0];
    EXPECT_EQ(email.studentId, kStudent);
    EXPECT_EQ(email.channel, Channel::Email);
    EXPECT_NEAR(email.sentAt, due - kOneDay, 5);  // about 24 h before due (+/- 5 min)
    EXPECT_THAT(email.subject, HasSubstr("Test HW 1"));
    EXPECT_THAT(email.body, HasSubstr("2026-11-21 12:10"));  // due date and time

    runScheduler(kT0 + 16, kT0 + 30);  // wait 15 more min, check again
    EXPECT_EQ(inbox.sent.size(), 1u) << "duplicate reminder sent";
}

}  // namespace
}  // namespace cramberry
