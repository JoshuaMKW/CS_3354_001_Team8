#include "schedule/NKScheduleRules.h"

#include <algorithm>
#include <set>
#include <utility>

namespace cramberry {
namespace {

bool validDay(Day day) {
    const int value = static_cast<int>(day);
    return value >= static_cast<int>(Day::Mon) && value <= static_cast<int>(Day::Sun);
}

bool validRange(const TimeRange& r) {
    return validDay(r.day) && r.start >= 0 && r.end <= kMinutesPerDay && r.start < r.end;
}

}  // namespace

std::vector<std::string> validateNKSchedule(
    const std::vector<StudyBlock>& blocks, const StudyPreferences& prefs,
    const std::vector<Assignment>& assignments) {
    std::vector<std::string> problems;

    if (prefs.studyHours.empty()) problems.push_back("Study hours must be configured");
    if (prefs.sessionMinutes <= 0 || prefs.sessionMinutes > kMinutesPerDay)
        problems.push_back("Invalid study-session length");
    if (prefs.breakMinutes < 0 || prefs.breakMinutes >= kMinutesPerDay)
        problems.push_back("Invalid break length");
    if (prefs.maxStudyMinutesPerDay <= 0 || prefs.maxStudyMinutesPerDay > kMinutesPerDay)
        problems.push_back("Invalid maximum daily study time");
    for (const auto& r : prefs.studyHours)
        if (!validRange(r)) problems.push_back("Study hours contain an invalid time range");
    for (const auto& r : prefs.personalTime)
        if (!validRange(r)) problems.push_back("Personal time contains an invalid time range");

    std::set<std::string> knownIds;
    for (const auto& a : assignments) {
        if (a.id.empty() || !knownIds.insert(a.id).second)
            problems.push_back("Assignments must have unique nonempty IDs");
        if (a.title.empty()) problems.push_back("Assignment titles must not be blank");
        if (!validDay(a.dueDay) || a.dueMinute < 0 || a.dueMinute > kMinutesPerDay)
            problems.push_back("Assignment has an invalid deadline");
        if (a.estimatedMinutes < 0) problems.push_back("Estimated duration cannot be negative");
    }

    if (blocks.size() > 500) problems.push_back("The schedule exceeds 500 study blocks");
    for (const auto& b : blocks) {
        if (!validRange(b.time)) problems.push_back("Study block has an invalid time range");
        if (b.title.empty()) problems.push_back("Study block title must not be blank");
        if (!knownIds.count(b.assignmentId))
            problems.push_back("Study block refers to an unknown assignment: " + b.assignmentId);
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        for (std::size_t j = i + 1; j < blocks.size(); ++j) {
            if (validRange(blocks[i].time) && validRange(blocks[j].time) &&
                blocks[i].time.overlaps(blocks[j].time)) {
                problems.push_back("Study blocks overlap: " + blocks[i].title + " and " + blocks[j].title);
            }
        }
    }

    // Avoid calling legacy checks with malformed days/times or invalid preferences.
    if (problems.empty()) {
        auto existing = findPreferenceViolations(blocks, prefs, assignments);
        problems.insert(problems.end(), existing.begin(), existing.end());
    }
    return problems;
}

}  // namespace cramberry
