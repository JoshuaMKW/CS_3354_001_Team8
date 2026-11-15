#pragma once

// Author: Ahnaf Adib (AA).
// Data passed from UC-10 Process Scheduling Request to UC-12 Validate AI Response, plus strict
// parsers for the date and time text the web app and Gemini send (used by UC-10, UC-11, UC-12).
// All times are UTC.

#include <cctype>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "common/Time.h"
#include "settings/Settings.h"

namespace cramberry {

namespace detail {

inline bool digitsAt(const std::string& s, std::initializer_list<std::size_t> positions) {
    for (std::size_t p : positions) {
        if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) return false;
    }
    return true;
}

inline int numberAt(const std::string& s, std::size_t pos, std::size_t len) {
    return std::stoi(s.substr(pos, len));
}

}  // namespace detail

// "2026-11-20" -> that date at 00:00 UTC. Rejects impossible dates such as 2026-02-30.
inline std::optional<Timestamp> parseIsoDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' ||
        !detail::digitsAt(s, {0, 1, 2, 3, 5, 6, 8, 9})) {
        return std::nullopt;
    }
    const int year = detail::numberAt(s, 0, 4);
    const auto month = static_cast<unsigned>(detail::numberAt(s, 5, 2));
    const auto day = static_cast<unsigned>(detail::numberAt(s, 8, 2));
    const std::chrono::year_month_day ymd{std::chrono::year{year}, std::chrono::month{month},
                                          std::chrono::day{day}};
    if (!ymd.ok()) return std::nullopt;
    return makeTimestamp(year, month, day, 0, 0);
}

// "18:30" -> minutes since midnight.
inline std::optional<int> parseClock(const std::string& s) {
    if (s.size() != 5 || s[2] != ':' || !detail::digitsAt(s, {0, 1, 3, 4})) return std::nullopt;
    const int hour = detail::numberAt(s, 0, 2);
    const int minute = detail::numberAt(s, 3, 2);
    if (hour > 23 || minute > 59) return std::nullopt;
    return hm(hour, minute);
}

// "2026-11-20T23:59", optionally followed by "Z" -> Timestamp.
inline std::optional<Timestamp> parseIsoMinute(std::string s) {
    if (!s.empty() && s.back() == 'Z') s.pop_back();
    if (s.size() != 16 || s[10] != 'T') return std::nullopt;
    const auto date = parseIsoDate(s.substr(0, 10));
    const auto clock = parseClock(s.substr(11));
    if (!date || !clock) return std::nullopt;
    return *date + *clock;
}

// A whole number written only with digits, e.g. "90". Rejects "", "-5", "1.5" and "12abc".
inline std::optional<int> parseWholeNumber(const std::string& s) {
    if (s.empty() || s.size() > 9) return std::nullopt;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return std::stoi(s);
}

// A task as stored by the Task Management System (Assignment in the UML class diagram).
struct TaskDetails {
    std::string id;
    std::string studentId;  // owner
    std::string title;
    Timestamp dueUtc = 0;
    int estimatedMinutes = 0;
    bool completed = false;

    bool operator==(const TaskDetails&) const = default;
};

// A request that passed UC-10 validation, ready for the Schedule System and the AI.
struct PreparedSchedulingRequest {
    std::string studentId;
    std::vector<TaskDetails> tasks;  // earliest deadline first
    StudyPreferences prefs;
    Timestamp submittedAt = 0;
};

// One study event in a validated, AI-generated schedule (ScheduleEvent in the UML class diagram).
struct ScheduledEvent {
    std::string assignmentId;
    std::string title;
    Timestamp start = 0;
    Timestamp end = 0;

    bool operator==(const ScheduledEvent&) const = default;
};

}  // namespace cramberry
