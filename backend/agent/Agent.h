#pragma once

#include <string>
#include <vector>

#include "schedule/Schedule.h"
#include "settings/Settings.h"

namespace cramberry {

struct Assignment {
    std::string id;
    std::string title;
    Day dueDay{};
    int dueMinute = 0;  // minutes since midnight on dueDay
    int estimatedMinutes = 0;
};

// Wraps the Gemini API call that proposes a study schedule.
class IAiClient {
public:
    virtual ~IAiClient() = default;
    virtual std::vector<StudyBlock> proposeSchedule(const StudyPreferences& prefs,
                                                    const std::vector<Assignment>& assignments) = 0;
};

// Every way a proposed schedule breaks the student's saved preferences.
// An empty result means the schedule follows all of them.
std::vector<std::string> findPreferenceViolations(const std::vector<StudyBlock>& blocks,
                                                  const StudyPreferences& prefs,
                                                  const std::vector<Assignment>& assignments);

struct GenerateResult {
    bool ok = false;
    std::vector<StudyBlock> blocks;       // empty unless ok
    std::vector<std::string> violations;  // why the AI's proposal was rejected
};

// Generates a study schedule with the AI and only accepts it if it follows the preferences.
class Agent {
public:
    explicit Agent(IAiClient& ai);
    GenerateResult generateStudySchedule(const StudyPreferences& prefs,
                                         const std::vector<Assignment>& assignments);

private:
    IAiClient& ai_;
};

}  // namespace cramberry
