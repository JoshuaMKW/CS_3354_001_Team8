#pragma once

// NK / UC-05: Generate AI Study Schedule. The existing Team 8 Agent wraps
// IAiClient (Gemini integration) and provides preference checks.
#include <optional>
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "schedule/Schedule.h"

namespace cramberry {

struct NKGenerationRequest {
    std::string studentId;  // Must come from authenticated server context.
    StudyPreferences preferences;
    std::vector<Assignment> assignments;
    std::optional<Day> onlyDay;  // If present, request a daily rather than weekly plan.
};

struct NKGenerationResponse {
    bool ok = false;
    std::vector<StudyBlock> schedule;
    std::vector<std::string> errors;
};

class NKStudyScheduleService {
public:
    NKStudyScheduleService(Agent& agent, IScheduleRepository& schedules);
    NKGenerationResponse generate(const NKGenerationRequest& request);

private:
    Agent& agent_;
    IScheduleRepository& schedules_;
};

}  // namespace cramberry
