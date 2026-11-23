#pragma once

#include <drogon/HttpAppFramework.h>

#include "settings/SettingsController.h"
#include "user/AuthGuard.h"

namespace cramberry {

// UC-15 Manage Study Preferences
// GET /api/settings/preferences
// PUT /api/settings/preferences  {studyHours, maxStudyMinutesPerDay, sessionMinutes,
//                                 breakMinutes, personalTime}
//   200 saved (the schedule is rebuilt in the background), 400 invalid,
//   409 personal time overlaps an accepted study block (nothing saved)
void registerSettingsRoutes(drogon::HttpAppFramework& app, SettingsController& controller,
                            const ISettingsRepository& settings, const AuthGuard& guard);

}  // namespace cramberry
