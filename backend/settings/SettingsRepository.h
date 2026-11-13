#pragma once

#include <optional>
#include <string>

#include "settings/Settings.h"

namespace cramberry {

// Storage for each student's PlannerSettings row (Supabase in production).
class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;
    virtual std::optional<StudyPreferences> find(const std::string& studentId) const = 0;
    // Returns false if the write failed; the stored row must then be left unchanged.
    virtual bool save(const std::string& studentId, const StudyPreferences& prefs) = 0;
};

}  // namespace cramberry
