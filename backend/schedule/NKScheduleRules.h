#pragma once

// Shared safety checks for NK's schedule generation and manual editing.
// Uses the Team 8 schedule and preference types; does not introduce a second model.
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "schedule/Schedule.h"
#include "settings/Settings.h"

namespace cramberry {

// Adds input, identity, overlap and time-bound checks to the existing
// findPreferenceViolations() function from backend/agent/Agent.cpp.
std::vector<std::string> validateNKSchedule(
    const std::vector<StudyBlock>& blocks,
    const StudyPreferences& prefs,
    const std::vector<Assignment>& assignments);

}  // namespace cramberry
