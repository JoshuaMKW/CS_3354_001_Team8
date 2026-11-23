#pragma once

// JSON shapes shared by the REST API, the database (preferences are stored as JSON)
// and the Gemini prompt. Times of day are "HH:MM" (24-hour), days are "Mon".."Sun",
// and absolute times are ISO 8601 in UTC ("2026-11-21T12:10:00Z").

#include <json/json.h>

#include <optional>
#include <string>
#include <string_view>

#include "common/Time.h"
#include "notifications/Notification.h"
#include "schedule/Schedule.h"
#include "settings/Settings.h"

namespace cramberry::json {

Timestamp nowMinutes();
// Monday 00:00 of the week containing t (both in the same clock, UTC or local).
Timestamp startOfWeek(Timestamp t);

std::optional<Day> parseDay(std::string_view text);
// "18:30" -> 1110. "24:00" is allowed so a range can run to midnight.
std::optional<int> parseClock(std::string_view text);
std::string formatClock(int minutesSinceMidnight);

// Accepts "YYYY-MM-DD", "YYYY-MM-DDTHH:MM", "YYYY-MM-DDTHH:MM:SS" with an optional "Z".
// A date on its own means 23:59 that day (end of day, for due dates).
std::optional<Timestamp> parseIsoTimestamp(std::string_view text);
// Like parseIsoTimestamp but a bare date means 00:00 (start of day, for report weeks).
std::optional<Timestamp> parseIsoDateStart(std::string_view text);
std::string formatIsoTimestamp(Timestamp t);

Json::Value toJson(const TimeRange& range);
std::optional<TimeRange> timeRangeFromJson(const Json::Value& value, std::string& error);

Json::Value toJson(const StudyBlock& block);
std::optional<StudyBlock> studyBlockFromJson(const Json::Value& value, std::string& error);

Json::Value toJson(const StudyPreferences& prefs);
std::optional<StudyPreferences> preferencesFromJson(const Json::Value& value, std::string& error);

Json::Value toJson(const NotificationSettings& settings);
std::optional<NotificationSettings> notificationSettingsFromJson(const Json::Value& value,
                                                                 std::string& error);

std::string channelName(Channel channel);
std::optional<Channel> parseChannel(std::string_view text);

std::string write(const Json::Value& value);  // compact, one line
std::optional<Json::Value> parse(std::string_view text);

}  // namespace cramberry::json
