#pragma once

// Bridges real tasks (absolute UTC due dates) and the planner's weekly model
// (agent/Agent.h: a due Day plus minutes since midnight, in the student's local time).

#include <optional>
#include <vector>

#include "agent/Agent.h"
#include "tasks/Task.h"

namespace cramberry {

struct PlanningWeek {
    Timestamp startUtc = 0;    // the student's local Monday 00:00, as a UTC timestamp
    int utcOffsetMinutes = 0;

    Timestamp endUtc() const { return startUtc + 7 * kMinutesPerDay; }
    // UTC start of a weekly block in this week.
    Timestamp toUtc(Day day, int minute) const {
        return startUtc + static_cast<int>(day) * kMinutesPerDay + minute;
    }
    // Local weekday/minute of a UTC time inside this week, or nothing if outside it.
    std::optional<std::pair<Day, int>> toLocal(Timestamp utc) const;
};

PlanningWeek planningWeek(Timestamp now, int utcOffsetMinutes);

// The study hours still ahead of `now` this week: earlier days are dropped and a range
// that has already started begins at the next 5-minute mark. Plans are built from these,
// so nothing is scheduled in the past.
StudyPreferences remainingStudyHours(const StudyPreferences& prefs, const PlanningWeek& week,
                                     Timestamp now);

// Tasks a weekly plan must make time for: everything unfinished that is overdue or
// due within the next 7 days. Anything due after this week, or already overdue,
// counts as due at the end of Sunday so the planner fits it in where it can.
std::vector<Assignment> plannerAssignments(const std::vector<Task>& tasks, const PlanningWeek& week,
                                           Timestamp now);

// Converts the given tasks with the same deadline mapping as plannerAssignments (overdue,
// or due after this week, means Sun 24:00) but without the horizon/status filter. Used to
// validate edits to blocks that were generated earlier, so a move is checked against the
// same deadlines the blocks were planned for.
std::vector<Assignment> toAssignments(const std::vector<Task>& tasks, const PlanningWeek& week,
                                      Timestamp now);

// UC-5 fallback planner: a deterministic, earliest-deadline-first schedule that follows
// every study preference. Used when Gemini isn't configured or its answer is rejected,
// so the AI being down never stops students from planning (NFR-15).
class LocalPlanner : public IAiClient {
public:
    std::vector<StudyBlock> proposeSchedule(const StudyPreferences& prefs,
                                            const std::vector<Assignment>& assignments) override;

    // Tasks that only partly fit, from the last proposeSchedule() call.
    const std::vector<std::string>& shortfalls() const { return shortfalls_; }

private:
    std::vector<std::string> shortfalls_;
};

}  // namespace cramberry
