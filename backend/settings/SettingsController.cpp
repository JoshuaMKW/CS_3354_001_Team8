#include "settings/SettingsController.h"

namespace cramberry {

namespace {

bool isValidRange(const TimeRange& r) {
    return r.start >= 0 && r.end <= kMinutesPerDay && r.start < r.end;
}

}  // namespace

SettingsController::SettingsController(ISettingsRepository& settings, IScheduleRepository& schedule,
                                       IScheduleRegenerator& regenerator)
    : settings_(settings), schedule_(schedule), regenerator_(regenerator) {}

std::optional<std::string> SettingsController::validate(const StudyPreferences& prefs) {
    if (prefs.sessionMinutes <= 0) {
        return "Session length must be greater than 0 min";
    }
    if (prefs.breakMinutes < 0) {
        return "Break can't be negative";
    }
    if (prefs.breakMinutes > prefs.sessionMinutes) {
        return "Break can't be longer than your " + std::to_string(prefs.sessionMinutes) +
               " min session";
    }
    if (prefs.maxStudyMinutesPerDay <= 0 || prefs.maxStudyMinutesPerDay > kMinutesPerDay) {
        return "Most study per day must be between 1 min and 24 hours";
    }
    for (const auto& r : prefs.studyHours) {
        if (!isValidRange(r)) return "Study hours must end after they start";
    }
    for (const auto& r : prefs.personalTime) {
        if (!isValidRange(r)) return "Personal time must end after it starts";
    }
    return std::nullopt;
}

SaveResult SettingsController::saveStudyPreferences(const std::string& studentId,
                                                    const StudyPreferences& prefs) {
    if (auto error = validate(prefs)) {
        return {SaveResult::Status::InvalidInput, *error, {}};
    }

    // CheckForConflicts(): new personal time must not cover an accepted study block.
    std::vector<StudyBlock> conflicts;
    for (const auto& block : schedule_.getActiveStudyItems(studentId)) {
        for (const auto& personal : prefs.personalTime) {
            if (personal.overlaps(block.time)) {
                conflicts.push_back(block);
                break;
            }
        }
    }
    if (!conflicts.empty()) {
        std::string message = "Personal time conflicts with";
        for (std::size_t i = 0; i < conflicts.size(); ++i) {
            message += (i == 0 ? " \"" : ", \"") + conflicts[i].title + "\" on " +
                       toString(conflicts[i].time);
        }
        return {SaveResult::Status::Conflict, message, conflicts};
    }

    if (!settings_.save(studentId, prefs)) {
        return {SaveResult::Status::Error, "Couldn't save your settings. Please try again.", {}};
    }
    regenerator_.regenerate(studentId, prefs);
    return {SaveResult::Status::Saved, "Settings saved.", {}};
}

}  // namespace cramberry
