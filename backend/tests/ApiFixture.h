#pragma once

// Shared setup for the REST API tests: a private in-memory SQLite database per test,
// so the tests never touch a real student's data, Supabase or Gemini.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "agent/AgentRepository.h"
#include "data-api/Database.h"
#include "notifications/NotificationRepository.h"
#include "schedule/ScheduleRepository.h"
#include "settings/SqlSettingsRepository.h"
#include "tasks/TaskController.h"
#include "tasks/TaskRepository.h"
#include "user/Crypto.h"
#include "user/UserRepository.h"

namespace cramberry::testing {

// Monday 23 November 2026, 12:00 UTC.
inline const Timestamp kNow = makeTimestamp(2026, 11, 23, 12, 0);
constexpr Timestamp kHour = 60;
constexpr Timestamp kDay = 24 * 60;

class ApiFixture : public ::testing::Test {
protected:
    std::shared_ptr<data::Database> db = data::Database::openInMemory();
    SqlUserRepository users{*db};
    SqlTaskRepository taskRepo{*db};
    SqlScheduleRepository schedules{*db};
    SqlSettingsRepository settings{*db};
    SqlNotificationRepository notificationSettings{*db};
    SqlInbox inbox{*db};
    SqlAgentRepository agentRuns{*db};
    TaskController tasks{taskRepo};

    std::string addStudent(const std::string& email, int utcOffsetMinutes = 0) {
        Student s{crypto::newId(), "Student " + email, email, utcOffsetMinutes, kNow};
        EXPECT_TRUE(users.create(s, crypto::hashPassword("password1", 1)));
        return s.id;
    }

    Task addTask(const std::string& studentId, const std::string& title, Timestamp due,
                 int estimatedMinutes = 60, int priority = 2) {
        Task draft;
        draft.title = title;
        draft.dueAt = due;
        draft.estimatedMinutes = estimatedMinutes;
        draft.priority = priority;
        auto created = tasks.createTask(studentId, draft, kNow);
        EXPECT_TRUE(created.ok()) << (created.ok() ? "" : created.error().message);
        return created.value();
    }

    static StudyPreferences weekdayEvenings() {
        StudyPreferences prefs;
        for (Day d : {Day::Mon, Day::Tue, Day::Wed, Day::Thu, Day::Fri}) {
            prefs.studyHours.push_back({d, hm(16), hm(21)});
        }
        prefs.maxStudyMinutesPerDay = 180;
        prefs.sessionMinutes = 60;
        prefs.breakMinutes = 15;
        return prefs;
    }
};

}  // namespace cramberry::testing
