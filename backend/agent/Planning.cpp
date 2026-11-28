#include "agent/Planning.h"

#include <algorithm>
#include <array>
#include <numeric>

#include "common/Json.h"

namespace cramberry {

namespace {

constexpr int kEndOfWeek = kMinutesPerDay;  // Sun 24:00
constexpr int kStep = 5;                    // planner granularity in minutes
constexpr int kMinChunk = 15;               // shortest block worth scheduling

Assignment toAssignment(const Task& task, Day day, int minute) {
    return {task.id, task.title, day, minute, task.estimatedMinutes};
}

int weekMinute(Day day, int minute) { return static_cast<int>(day) * kMinutesPerDay + minute; }

}  // namespace

std::optional<std::pair<Day, int>> PlanningWeek::toLocal(Timestamp utc) const {
    if (utc < startUtc || utc >= endUtc()) return std::nullopt;
    const auto offset = utc - startUtc;
    return std::make_pair(static_cast<Day>(offset / kMinutesPerDay),
                          static_cast<int>(offset % kMinutesPerDay));
}

PlanningWeek planningWeek(Timestamp now, int utcOffsetMinutes) {
    const Timestamp localMonday = json::startOfWeek(now + utcOffsetMinutes);
    return {localMonday - utcOffsetMinutes, utcOffsetMinutes};
}

StudyPreferences remainingStudyHours(const StudyPreferences& prefs, const PlanningWeek& week,
                                     Timestamp now) {
    StudyPreferences out = prefs;
    out.studyHours.clear();
    const Timestamp sinceMonday = std::max<Timestamp>(0, now - week.startUtc);
    const int ceiled = static_cast<int>((sinceMonday + kStep - 1) / kStep * kStep);
    for (const auto& range : prefs.studyHours) {
        const int start = std::max(weekMinute(range.day, range.start), ceiled);
        const int end = weekMinute(range.day, range.end);
        if (end - start >= kMinChunk) {
            out.studyHours.push_back({range.day, start - weekMinute(range.day, 0), range.end});
        }
    }
    return out;
}

std::vector<Assignment> toAssignments(const std::vector<Task>& tasks, const PlanningWeek& week,
                                      Timestamp now) {
    std::vector<Assignment> out;
    for (const auto& task : tasks) {
        const auto local = task.dueAt < now ? std::nullopt : week.toLocal(task.dueAt);
        out.push_back(local ? toAssignment(task, local->first, local->second)
                            : toAssignment(task, Day::Sun, kEndOfWeek));
    }
    return out;
}

std::vector<Assignment> plannerAssignments(const std::vector<Task>& tasks, const PlanningWeek& week,
                                           Timestamp now) {
    std::vector<Task> planned;
    for (const auto& task : tasks) {
        if (task.status == TaskStatus::Completed) continue;
        if (task.dueAt >= now + 7 * kMinutesPerDay) continue;
        planned.push_back(task);
    }
    return toAssignments(planned, week, now);
}

std::vector<StudyBlock> LocalPlanner::proposeSchedule(const StudyPreferences& prefs,
                                                      const std::vector<Assignment>& assignments) {
    shortfalls_.clear();
    std::vector<StudyBlock> blocks;
    std::array<int, 7> usedPerDay{};

    // Earliest deadline first; ties go to the bigger job.
    std::vector<std::size_t> order(assignments.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& x = assignments[a];
        const auto& y = assignments[b];
        const int dx = weekMinute(x.dueDay, x.dueMinute), dy = weekMinute(y.dueDay, y.dueMinute);
        return dx != dy ? dx < dy : x.estimatedMinutes > y.estimatedMinutes;
    });

    for (const std::size_t index : order) {
        const Assignment& a = assignments[index];
        int remaining = a.estimatedMinutes > 0 ? a.estimatedMinutes : std::min(30, prefs.sessionMinutes);
        const int due = weekMinute(a.dueDay, a.dueMinute);

        for (int d = 0; d < 7 && remaining > 0; ++d) {
            const Day day = static_cast<Day>(d);
            std::vector<TimeRange> hours;
            for (const auto& r : prefs.studyHours) {
                if (r.day == day) hours.push_back(r);
            }
            std::sort(hours.begin(), hours.end(),
                      [](const TimeRange& x, const TimeRange& y) { return x.start < y.start; });

            for (const auto& range : hours) {
                int t = range.start;
                while (t < range.end && remaining > 0) {
                    if (usedPerDay[d] >= prefs.maxStudyMinutesPerDay) break;
                    int limit = std::min(range.end, due - d * kMinutesPerDay);
                    int next = t + kStep;
                    bool blocked = limit <= t;

                    for (const auto& p : prefs.personalTime) {
                        if (p.day != day) continue;
                        if (p.start <= t && t < p.end) {
                            blocked = true;
                            next = std::max(next, p.end);
                        } else if (p.start > t) {
                            limit = std::min(limit, p.start);
                        }
                    }
                    for (const auto& b : blocks) {
                        if (b.time.day != day) continue;
                        // A new block needs a full break before and after every existing one.
                        if (t >= b.time.start - prefs.breakMinutes && t < b.time.end + prefs.breakMinutes) {
                            blocked = true;
                            next = std::max(next, b.time.end + prefs.breakMinutes);
                        } else if (b.time.start - prefs.breakMinutes > t) {
                            limit = std::min(limit, b.time.start - prefs.breakMinutes);
                        }
                    }

                    const int length = std::min({limit - t, remaining, prefs.sessionMinutes,
                                                 prefs.maxStudyMinutesPerDay - usedPerDay[d]});
                    if (!blocked && length > 0 && length >= std::min(remaining, kMinChunk)) {
                        blocks.push_back({a.title, a.id, {day, t, t + length}});
                        usedPerDay[d] += length;
                        remaining -= length;
                        t += length + prefs.breakMinutes;
                    } else {
                        t = next;
                    }
                }
            }
        }
        if (remaining > 0) {
            const int wanted = a.estimatedMinutes > 0 ? a.estimatedMinutes : remaining;
            shortfalls_.push_back("Only " + std::to_string(wanted - remaining) + " of " +
                                  std::to_string(wanted) + " min for \"" + a.title +
                                  "\" fit before it is due");
        }
    }
    return blocks;
}

}  // namespace cramberry
