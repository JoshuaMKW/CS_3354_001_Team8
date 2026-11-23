#include <drogon/orm/Exception.h>
#include <trantor/utils/Logger.h>

#include "common/Json.h"
#include "settings/SqlSettingsRepository.h"

namespace cramberry {

std::optional<StudyPreferences> SqlSettingsRepository::find(const std::string& studentId) const {
    const auto rows =
        db_.exec("SELECT preferences FROM study_preferences WHERE student_id = $1", studentId);
    if (rows.empty()) return std::nullopt;
    const auto parsed = json::parse(rows[0]["preferences"].as<std::string>());
    std::string error;
    if (!parsed) return std::nullopt;
    return json::preferencesFromJson(*parsed, error);
}

bool SqlSettingsRepository::save(const std::string& studentId, const StudyPreferences& prefs) {
    try {
        // A single upsert: either the new row is stored or the old one is untouched.
        db_.exec(
            "INSERT INTO study_preferences (student_id, preferences, updated_at) VALUES ($1, $2, $3) "
            "ON CONFLICT (student_id) DO UPDATE SET preferences = excluded.preferences, "
            "updated_at = excluded.updated_at",
            studentId, json::write(json::toJson(prefs)), static_cast<std::int64_t>(json::nowMinutes()));
        return true;
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Saving study preferences failed: " << e.base().what();
        return false;
    }
}

}  // namespace cramberry
