#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace cramberry {

enum class Day { Mon, Tue, Wed, Thu, Fri, Sat, Sun };

constexpr int kMinutesPerDay = 24 * 60;

// Minutes since midnight, e.g. hm(18, 30) == 6:30 PM.
constexpr int hm(int hour, int minute = 0) { return hour * 60 + minute; }

// A weekly time slot such as "Tue 6:00 PM-7:30 PM". start/end are minutes since midnight.
struct TimeRange {
    Day day{};
    int start = 0;
    int end = 0;

    int length() const { return end - start; }
    bool contains(const TimeRange& other) const {
        return day == other.day && start <= other.start && other.end <= end;
    }
    bool overlaps(const TimeRange& other) const {
        return day == other.day && start < other.end && other.start < end;
    }
    bool operator==(const TimeRange&) const = default;
};

inline std::string dayName(Day day) {
    static const char* const names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return names[static_cast<int>(day)];
}

inline std::string clock12(int minutesSinceMidnight) {
    const int hour = minutesSinceMidnight / 60;
    const int minute = minutesSinceMidnight % 60;
    const int hour12 = hour % 12 == 0 ? 12 : hour % 12;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d %s", hour12, minute, hour < 12 ? "AM" : "PM");
    return buf;
}

inline std::string toString(const TimeRange& range) {
    return dayName(range.day) + " " + clock12(range.start) + "-" + clock12(range.end);
}

// Absolute point in time: minutes since the Unix epoch (UTC).
using Timestamp = std::int64_t;

inline Timestamp makeTimestamp(int year, unsigned month, unsigned day, int hour, int minute) {
    using namespace std::chrono;
    const sys_days date = year_month_day{std::chrono::year{year}, std::chrono::month{month},
                                         std::chrono::day{day}};
    return duration_cast<minutes>(date.time_since_epoch()).count() + hm(hour, minute);
}

// "2026-11-21 12:10 UTC"
inline std::string formatTimestamp(Timestamp t) {
    using namespace std::chrono;
    const sys_time<minutes> tp{minutes{t}};
    const sys_days date = floor<days>(tp);
    const year_month_day ymd{date};
    const auto sinceMidnight = (tp - date).count();
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u %02d:%02d UTC", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                  static_cast<int>(sinceMidnight / 60), static_cast<int>(sinceMidnight % 60));
    return buf;
}

}  // namespace cramberry
