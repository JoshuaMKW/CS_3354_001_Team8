#pragma once

#include <string>
#include <vector>

#include "agent/Planning.h"
#include "common/Outcome.h"
#include "schedule/Schedule.h"
#include "settings/SettingsRepository.h"
#include "tasks/Task.h"
#include "user/User.h"

namespace cramberry {

// SOW "Student Dashboard": upcoming work, priorities and the generated plan.
struct Dashboard {
    Student student;
    PlanningWeek week;
    Day today = Day::Mon;
    bool hasPreferences = false;
    std::vector<StudyBlock> todaysBlocks;
    std::vector<StudyBlock> schedule;
    std::vector<Task> upcoming;  // unfinished, due in the next 14 days, highest priority first
    std::vector<Task> overdue;   // FR-26
    int notStarted = 0;
    int inProgress = 0;
    int completedThisWeek = 0;
};

struct BlockMove {
    std::string assignmentId;
    TimeRange from;
    TimeRange to;
    std::string title;  // empty keeps the current title
};

struct BlockRef {
    std::string assignmentId;
    TimeRange time;
};

// UC-4 View Schedule, UC-6 Modify AI Schedule (FR-17), UC-8 Export Schedule (FR-10).
class ScheduleController {
public:
    ScheduleController(IScheduleRepository& schedules, const ISettingsRepository& settings,
                       const ITaskRepository& tasks, const IUserRepository& users)
        : schedules_(schedules), settings_(settings), tasks_(tasks), users_(users) {}

    Outcome<PlanningWeek> week(const std::string& studentId, Timestamp now) const;
    std::vector<StudyBlock> current(const std::string& studentId) const;
    // Moves one block; the result must still follow every study preference.
    Outcome<std::vector<StudyBlock>> moveBlock(const std::string& studentId, const BlockMove& move,
                                               Timestamp now);
    // Removes one block. Fewer study blocks can't break a preference, so nothing is re-checked.
    Outcome<std::vector<StudyBlock>> removeBlock(const std::string& studentId, const BlockRef& block);
    void clear(const std::string& studentId);
    // iCalendar (RFC 5545) with this week's study blocks and upcoming deadlines.
    Outcome<std::string> exportIcs(const std::string& studentId, Timestamp now) const;
    Outcome<Dashboard> dashboard(const std::string& studentId, Timestamp now) const;

private:
    IScheduleRepository& schedules_;
    const ISettingsRepository& settings_;
    const ITaskRepository& tasks_;
    const IUserRepository& users_;
};

}  // namespace cramberry
