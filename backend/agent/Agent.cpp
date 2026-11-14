#include "agent/Agent.h"

#include <algorithm>
#include <map>

namespace cramberry {

namespace {

int weekMinute(Day day, int minute) { return static_cast<int>(day) * kMinutesPerDay + minute; }

std::string describe(const StudyBlock& b) { return "\"" + b.title + "\" (" + toString(b.time) + ")"; }

}  // namespace

std::vector<std::string> findPreferenceViolations(const std::vector<StudyBlock>& blocks,
                                                  const StudyPreferences& prefs,
                                                  const std::vector<Assignment>& assignments) {
    std::vector<std::string> violations;
    std::map<Day, int> minutesPerDay;
    std::map<Day, std::vector<TimeRange>> rangesPerDay;

    for (const auto& b : blocks) {
        if (b.time.length() <= 0) {
            violations.push_back(describe(b) + " has no length");
            continue;
        }
        const bool insideStudyHours = std::any_of(prefs.studyHours.begin(), prefs.studyHours.end(),
                                                  [&](const TimeRange& r) { return r.contains(b.time); });
        if (!insideStudyHours) {
            violations.push_back(describe(b) + " is outside study hours");
        }
        for (const auto& personal : prefs.personalTime) {
            if (personal.overlaps(b.time)) {
                violations.push_back(describe(b) + " overlaps personal time " + toString(personal));
            }
        }
        if (b.time.length() > prefs.sessionMinutes) {
            violations.push_back(describe(b) + " is longer than the " +
                                 std::to_string(prefs.sessionMinutes) + " min session length");
        }
        minutesPerDay[b.time.day] += b.time.length();
        rangesPerDay[b.time.day].push_back(b.time);
    }

    for (const auto& [day, total] : minutesPerDay) {
        if (total > prefs.maxStudyMinutesPerDay) {
            violations.push_back(dayName(day) + " has " + std::to_string(total) +
                                 " min of study (max " +
                                 std::to_string(prefs.maxStudyMinutesPerDay) + ")");
        }
    }

    for (auto& [day, ranges] : rangesPerDay) {
        std::sort(ranges.begin(), ranges.end(),
                  [](const TimeRange& a, const TimeRange& b) { return a.start < b.start; });
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].start - ranges[i - 1].end < prefs.breakMinutes) {
                violations.push_back(toString(ranges[i]) + " starts less than " +
                                     std::to_string(prefs.breakMinutes) +
                                     " min after the previous block");
            }
        }
    }

    for (const auto& a : assignments) {
        bool scheduled = false;
        for (const auto& b : blocks) {
            if (b.assignmentId != a.id) continue;
            scheduled = true;
            if (weekMinute(b.time.day, b.time.end) > weekMinute(a.dueDay, a.dueMinute)) {
                violations.push_back(describe(b) + " ends after \"" + a.title + "\" is due");
            }
        }
        if (!scheduled) {
            violations.push_back("\"" + a.title + "\" is not scheduled");
        }
    }
    return violations;
}

Agent::Agent(IAiClient& ai) : ai_(ai) {}

GenerateResult Agent::generateStudySchedule(const StudyPreferences& prefs,
                                            const std::vector<Assignment>& assignments) {
    auto proposed = ai_.proposeSchedule(prefs, assignments);
    auto violations = findPreferenceViolations(proposed, prefs, assignments);
    if (!violations.empty()) {
        return {false, {}, std::move(violations)};
    }
    return {true, std::move(proposed), {}};
}

}  // namespace cramberry
