#pragma once

#include <optional>
#include <string>
#include <vector>

#include "schedule/Schedule.h"
#include "settings/SettingsRepository.h"

namespace cramberry {

// Asks the AI Scheduling System to rebuild the schedule after preferences change.
class IScheduleRegenerator {
public:
    virtual ~IScheduleRegenerator() = default;
    virtual void regenerate(const std::string& studentId, const StudyPreferences& prefs) = 0;
};

struct SaveResult {
    enum class Status { Saved, InvalidInput, Conflict, Error };

    Status status = Status::Error;
    std::string message;
    std::vector<StudyBlock> conflicts;  // filled when status == Conflict
};

// UC-15 Manage Study Preferences. Follows the JA sequence diagram:
// validate -> GetActiveStudyItems() -> CheckForConflicts() ->
//   [no conflicts] save + GenerateSchedule() -> Confirmation
//   [exception]    Show Conflict (nothing saved, nothing regenerated)
class SettingsController {
public:
    SettingsController(ISettingsRepository& settings, IScheduleRepository& schedule,
                       IScheduleRegenerator& regenerator);

    SaveResult saveStudyPreferences(const std::string& studentId, const StudyPreferences& prefs);

    // Returns the error message to show, or nothing if the preferences are valid.
    static std::optional<std::string> validate(const StudyPreferences& prefs);

private:
    ISettingsRepository& settings_;
    IScheduleRepository& schedule_;
    IScheduleRegenerator& regenerator_;
};

}  // namespace cramberry
