#pragma once

#include "data-api/Database.h"
#include "settings/SettingsRepository.h"

namespace cramberry {

// SettingsRepository.cpp in the Development View: each student's study
// preferences (UC-15), stored as one JSON document per student.
class SqlSettingsRepository : public ISettingsRepository {
public:
    explicit SqlSettingsRepository(const data::Database& db) : db_(db) {}

    std::optional<StudyPreferences> find(const std::string& studentId) const override;
    bool save(const std::string& studentId, const StudyPreferences& prefs) override;

private:
    const data::Database& db_;
};

}  // namespace cramberry
