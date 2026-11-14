#pragma once

#include <vector>

#include "common/Time.h"

namespace cramberry {

// UC-15 Manage Study Preferences: the student's study habits and constraints
// (PlannerSettings in the UML class diagram).
struct StudyPreferences {
    std::vector<TimeRange> studyHours;    // when the student is willing to study
    int maxStudyMinutesPerDay = 180;      // "Most study per day"
    int sessionMinutes = 60;              // "Session length"
    int breakMinutes = 15;                // "Break after each session"
    std::vector<TimeRange> personalTime;  // blocked time, never scheduled

    bool operator==(const StudyPreferences&) const = default;
};

}  // namespace cramberry
