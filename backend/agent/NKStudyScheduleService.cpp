#include "agent/NKStudyScheduleService.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "schedule/NKScheduleRules.h"

namespace cramberry {

NKStudyScheduleService::NKStudyScheduleService(Agent& agent, IScheduleRepository& schedules)
    : agent_(agent), schedules_(schedules) {}

NKGenerationResponse NKStudyScheduleService::generate(const NKGenerationRequest& request) {
    if (request.studentId.empty()) return {false, {}, {"Authenticated student ID is required"}};
    if (request.assignments.empty()) return {false, {}, {"Provide at least one assignment"}};

    StudyPreferences effective = request.preferences;
    if (request.onlyDay.has_value()) {
        effective.studyHours.erase(
            std::remove_if(effective.studyHours.begin(), effective.studyHours.end(),
                [&](const TimeRange& r) { return r.day != *request.onlyDay; }),
            effective.studyHours.end());
        if (effective.studyHours.empty())
            return {false, {}, {"There are no available study hours on the selected day"}};
    }

    // Restricting studyHours also communicates daily scope to IAiClient.
    try {
        const GenerateResult proposed = agent_.generateStudySchedule(
            effective, request.assignments);
        if (!proposed.ok) return {false, {}, proposed.violations};
        if (proposed.blocks.empty()) return {false, {}, {"AI produced an empty schedule"}};

        auto errors = validateNKSchedule(proposed.blocks, effective, request.assignments);
        if (request.onlyDay.has_value()) {
            for (const auto& block : proposed.blocks)
                if (block.time.day != *request.onlyDay)
                    errors.push_back("Daily plan contains a block on another day");
        }
        if (!errors.empty()) return {false, {}, std::move(errors)};

        // The implementation of IScheduleRepository::replace MUST update all
        // blocks atomically in production. In-memory tests use a map.
        schedules_.replace(request.studentId, proposed.blocks);
        return {true, proposed.blocks, {}};
    } catch (const std::exception&) {
        return {false, {}, {"AI or schedule storage is currently unavailable; saved plan was not intentionally replaced"}};
    }
}

}  // namespace cramberry
