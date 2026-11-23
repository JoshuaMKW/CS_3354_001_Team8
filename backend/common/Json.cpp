#include "common/Json.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>

namespace cramberry::json {

namespace {

constexpr std::array<std::string_view, 7> kDays{"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

bool readDigits(std::string_view text, std::size_t pos, std::size_t count, int& out) {
    if (pos + count > text.size()) return false;
    int value = 0;
    for (std::size_t i = pos; i < pos + count; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

std::optional<Timestamp> parseIso(std::string_view text, bool dateOnlyMeansEndOfDay) {
    int year = 0, month = 0, day = 0;
    if (text.size() < 10 || text[4] != '-' || text[7] != '-' || !readDigits(text, 0, 4, year) ||
        !readDigits(text, 5, 2, month) || !readDigits(text, 8, 2, day)) {
        return std::nullopt;
    }
    const std::chrono::year_month_day ymd{std::chrono::year{year},
                                          std::chrono::month{static_cast<unsigned>(month)},
                                          std::chrono::day{static_cast<unsigned>(day)}};
    if (!ymd.ok()) return std::nullopt;

    int hour = dateOnlyMeansEndOfDay ? 23 : 0;
    int minute = dateOnlyMeansEndOfDay ? 59 : 0;
    std::string_view rest = text.substr(10);
    if (!rest.empty() && (rest[0] == 'T' || rest[0] == ' ')) {
        if (rest.size() < 6 || rest[3] != ':' || !readDigits(rest, 1, 2, hour) ||
            !readDigits(rest, 4, 2, minute)) {
            return std::nullopt;
        }
        rest = rest.substr(6);
        int second = 0;
        if (!rest.empty() && rest[0] == ':') {
            if (!readDigits(rest, 1, 2, second)) return std::nullopt;
            rest = rest.substr(3);
            // Drop fractional seconds such as ".000" from JavaScript's toISOString().
            if (!rest.empty() && rest[0] == '.') {
                std::size_t i = 1;
                while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') ++i;
                rest = rest.substr(i);
            }
        }
        if (hour > 23 || minute > 59 || second > 59) return std::nullopt;
    }
    if (rest == "Z" || rest == "+00:00") rest = {};
    if (!rest.empty()) return std::nullopt;  // only UTC is accepted
    return makeTimestamp(year, static_cast<unsigned>(month), static_cast<unsigned>(day), hour,
                         minute);
}

bool readInt(const Json::Value& obj, const char* key, int& out, std::string& error) {
    if (!obj.isMember(key)) return true;
    if (!obj[key].isInt()) {
        error = std::string(key) + " must be a whole number";
        return false;
    }
    out = obj[key].asInt();
    return true;
}

bool readBool(const Json::Value& obj, const char* key, bool& out, std::string& error) {
    if (!obj.isMember(key)) return true;
    if (!obj[key].isBool()) {
        error = std::string(key) + " must be true or false";
        return false;
    }
    out = obj[key].asBool();
    return true;
}

bool readRanges(const Json::Value& obj, const char* key, std::vector<TimeRange>& out,
                std::string& error) {
    if (!obj.isMember(key)) return true;
    if (!obj[key].isArray()) {
        error = std::string(key) + " must be a list";
        return false;
    }
    out.clear();
    for (const auto& item : obj[key]) {
        auto range = timeRangeFromJson(item, error);
        if (!range) {
            error = std::string(key) + ": " + error;
            return false;
        }
        out.push_back(*range);
    }
    return true;
}

Json::Value rangesToJson(const std::vector<TimeRange>& ranges) {
    Json::Value list(Json::arrayValue);
    for (const auto& r : ranges) list.append(toJson(r));
    return list;
}

}  // namespace

Timestamp nowMinutes() {
    using namespace std::chrono;
    return duration_cast<minutes>(system_clock::now().time_since_epoch()).count();
}

Timestamp startOfWeek(Timestamp t) {
    using namespace std::chrono;
    const sys_days day = floor<days>(sys_time<minutes>{minutes{t}});
    const sys_days monday = day - days{(weekday{day}.c_encoding() + 6) % 7};
    return duration_cast<minutes>(monday.time_since_epoch()).count();
}

std::optional<Day> parseDay(std::string_view text) {
    for (std::size_t i = 0; i < kDays.size(); ++i) {
        if (text == kDays[i]) return static_cast<Day>(i);
    }
    return std::nullopt;
}

std::optional<int> parseClock(std::string_view text) {
    int hour = 0, minute = 0;
    if (text.size() == 4) {  // "9:30"
        if (text[1] != ':' || !readDigits(text, 0, 1, hour) || !readDigits(text, 2, 2, minute)) {
            return std::nullopt;
        }
    } else if (text.size() != 5 || text[2] != ':' || !readDigits(text, 0, 2, hour) ||
               !readDigits(text, 3, 2, minute)) {
        return std::nullopt;
    }
    if (minute > 59 || hour > 24 || (hour == 24 && minute != 0)) return std::nullopt;
    return hm(hour, minute);
}

std::string formatClock(int minutesSinceMidnight) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", minutesSinceMidnight / 60, minutesSinceMidnight % 60);
    return buf;
}

std::optional<Timestamp> parseIsoTimestamp(std::string_view text) { return parseIso(text, true); }
std::optional<Timestamp> parseIsoDateStart(std::string_view text) { return parseIso(text, false); }

std::string formatIsoTimestamp(Timestamp t) {
    using namespace std::chrono;
    const sys_time<minutes> tp{minutes{t}};
    const sys_days date = floor<days>(tp);
    const year_month_day ymd{date};
    const auto sinceMidnight = (tp - date).count();
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:00Z", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                  static_cast<int>(sinceMidnight / 60), static_cast<int>(sinceMidnight % 60));
    return buf;
}

Json::Value toJson(const TimeRange& range) {
    Json::Value v;
    v["day"] = dayName(range.day);
    v["start"] = formatClock(range.start);
    v["end"] = formatClock(range.end);
    return v;
}

std::optional<TimeRange> timeRangeFromJson(const Json::Value& value, std::string& error) {
    if (!value.isObject() || !value["day"].isString() || !value["start"].isString() ||
        !value["end"].isString()) {
        error = "a time range needs day, start and end, e.g. {\"day\":\"Mon\",\"start\":\"16:00\",\"end\":\"19:00\"}";
        return std::nullopt;
    }
    const auto day = parseDay(value["day"].asString());
    const auto start = parseClock(value["start"].asString());
    const auto end = parseClock(value["end"].asString());
    if (!day) {
        error = "day must be one of Mon, Tue, Wed, Thu, Fri, Sat, Sun";
        return std::nullopt;
    }
    if (!start || !end) {
        error = "start and end must be times like 16:00";
        return std::nullopt;
    }
    return TimeRange{*day, *start, *end};
}

Json::Value toJson(const StudyBlock& block) {
    Json::Value v = toJson(block.time);
    v["title"] = block.title;
    v["assignmentId"] = block.assignmentId;
    return v;
}

std::optional<StudyBlock> studyBlockFromJson(const Json::Value& value, std::string& error) {
    auto range = timeRangeFromJson(value, error);
    if (!range) return std::nullopt;
    if (!value["assignmentId"].isString()) {
        error = "assignmentId is required";
        return std::nullopt;
    }
    StudyBlock block;
    block.time = *range;
    block.assignmentId = value["assignmentId"].asString();
    block.title = value["title"].isString() ? value["title"].asString() : "";
    return block;
}

Json::Value toJson(const StudyPreferences& prefs) {
    Json::Value v;
    v["studyHours"] = rangesToJson(prefs.studyHours);
    v["maxStudyMinutesPerDay"] = prefs.maxStudyMinutesPerDay;
    v["sessionMinutes"] = prefs.sessionMinutes;
    v["breakMinutes"] = prefs.breakMinutes;
    v["personalTime"] = rangesToJson(prefs.personalTime);
    return v;
}

std::optional<StudyPreferences> preferencesFromJson(const Json::Value& value, std::string& error) {
    if (!value.isObject()) {
        error = "study preferences must be a JSON object";
        return std::nullopt;
    }
    StudyPreferences prefs;
    if (!readRanges(value, "studyHours", prefs.studyHours, error) ||
        !readInt(value, "maxStudyMinutesPerDay", prefs.maxStudyMinutesPerDay, error) ||
        !readInt(value, "sessionMinutes", prefs.sessionMinutes, error) ||
        !readInt(value, "breakMinutes", prefs.breakMinutes, error) ||
        !readRanges(value, "personalTime", prefs.personalTime, error)) {
        return std::nullopt;
    }
    return prefs;
}

Json::Value toJson(const NotificationSettings& s) {
    Json::Value v;
    v["enabled"] = s.enabled;
    v["email"] = s.email;
    v["desktop"] = s.desktop;
    v["sms"] = s.sms;
    v["phoneNumber"] = s.phoneNumber;
    v["deadlineReminders"] = s.deadlineReminders;
    v["deadlineLeadMinutes"] = s.deadlineLeadMinutes;
    return v;
}

std::optional<NotificationSettings> notificationSettingsFromJson(const Json::Value& value,
                                                                 std::string& error) {
    if (!value.isObject()) {
        error = "notification settings must be a JSON object";
        return std::nullopt;
    }
    NotificationSettings s;
    if (!readBool(value, "enabled", s.enabled, error) || !readBool(value, "email", s.email, error) ||
        !readBool(value, "desktop", s.desktop, error) || !readBool(value, "sms", s.sms, error) ||
        !readBool(value, "deadlineReminders", s.deadlineReminders, error) ||
        !readInt(value, "deadlineLeadMinutes", s.deadlineLeadMinutes, error)) {
        return std::nullopt;
    }
    if (value.isMember("phoneNumber")) {
        if (!value["phoneNumber"].isString()) {
            error = "phoneNumber must be text";
            return std::nullopt;
        }
        s.phoneNumber = value["phoneNumber"].asString();
    }
    return s;
}

std::string channelName(Channel channel) {
    switch (channel) {
        case Channel::Email: return "email";
        case Channel::Desktop: return "desktop";
        case Channel::Sms: return "sms";
    }
    return "desktop";
}

std::optional<Channel> parseChannel(std::string_view text) {
    if (text == "email") return Channel::Email;
    if (text == "desktop") return Channel::Desktop;
    if (text == "sms") return Channel::Sms;
    return std::nullopt;
}

std::string write(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::optional<Json::Value> parse(std::string_view text) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value;
    std::string errors;
    if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors)) {
        return std::nullopt;
    }
    return value;
}

}  // namespace cramberry::json
