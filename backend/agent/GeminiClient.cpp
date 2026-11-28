#include "agent/GeminiClient.h"

#include <algorithm>
#include <stdexcept>

#include "common/Json.h"

namespace cramberry {

namespace {

constexpr const char* kScheduleInstruction =
    "You are the scheduling engine of Cramberry, a student study planner. "
    "Build this week's study schedule (the student's local time) that gives every assignment "
    "enough study time before it is due. The input lists \"slots\": every time the student can "
    "study, already sized to one session, spaced by the break, clear of personal time and within the daily maximum. "
    "Follow every rule exactly; a schedule that breaks any rule is thrown away:\n"
    "1. Each block is one slot: copy its day, start and end exactly. You may make a block shorter "
    "by moving its end earlier, but never move its start, never make it longer, and never merge "
    "slots.\n"
    "2. Use each slot at most once, and only the slots listed. Days with no slots are unavailable.\n"
    "3. Every assignment gets at least one block, and its blocks end before its due day and time.\n"
    "4. Aim for each assignment's estimatedMinutes in total; schedule earlier deadlines first. "
    "When there aren't enough slots for everything, schedule less time rather than break a rule.\n"
    "5. Use the assignment's exact id as assignmentId and its title as title.\n"
    "Times are 24-hour \"HH:MM\". Days are Mon, Tue, Wed, Thu, Fri, Sat, Sun. "
    "Reply with JSON only, in this shape: "
    "{\"blocks\":[{\"assignmentId\":\"...\",\"title\":\"...\",\"day\":\"Mon\",\"start\":\"16:00\",\"end\":\"17:00\"}]}";

// OpenRouter structured output: {"blocks":[{"assignmentId","title","day","start","end"}]}.
Json::Value scheduleResponseFormat() {
    Json::Value text;
    text["type"] = "string";
    Json::Value day = text;
    for (const char* d : {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}) day["enum"].append(d);
    Json::Value clock = text;  // 24-hour "HH:MM"; without it Haiku sometimes wrote "4:00 PM"
    clock["pattern"] = "^([01][0-9]|2[0-3]):[0-5][0-9]$";

    Json::Value block;
    block["type"] = "object";
    block["additionalProperties"] = false;
    for (const char* field : {"assignmentId", "title", "day", "start", "end"}) {
        block["required"].append(field);
        const std::string name = field;
        block["properties"][field] = name == "day" ? day : (name == "start" || name == "end") ? clock : text;
    }

    Json::Value schema;
    schema["type"] = "object";
    schema["additionalProperties"] = false;
    schema["required"].append("blocks");
    schema["properties"]["blocks"]["type"] = "array";
    schema["properties"]["blocks"]["items"] = block;

    Json::Value format;
    format["type"] = "json_schema";
    format["json_schema"]["name"] = "study_schedule";
    format["json_schema"]["strict"] = true;
    format["json_schema"]["schema"] = schema;
    return format;
}

std::string stripCodeFence(std::string text) {
    const auto first = text.find("```");
    if (first == std::string::npos) return text;
    const auto bodyStart = text.find('\n', first);
    const auto last = text.rfind("```");
    if (bodyStart == std::string::npos || last <= bodyStart) return text;
    return text.substr(bodyStart + 1, last - bodyStart - 1);
}

}  // namespace

GeminiClient::GeminiClient(GeminiConfig config)
    : config_(std::move(config)), loopThread_("gemini") {
    loopThread_.run();
}

std::vector<TimeRange> GeminiClient::studySlots(const StudyPreferences& prefs) {
    std::vector<TimeRange> ranges = prefs.studyHours;
    std::sort(ranges.begin(), ranges.end(), [](const TimeRange& a, const TimeRange& b) {
        return std::pair(a.day, a.start) < std::pair(b.day, b.start);
    });
    constexpr int kShortest = 15;
    std::vector<TimeRange> slots;
    for (const auto& range : ranges) {
        int t = range.start;
        if (!slots.empty() && slots.back().day == range.day) {
            t = std::max(t, slots.back().end + prefs.breakMinutes);
        }
        while (range.end - t >= kShortest) {
            TimeRange slot{range.day, t, std::min(t + prefs.sessionMinutes, range.end)};
            // Stop the slot at the first personal time it runs into.
            int blockedUntil = -1;
            for (const auto& personal : prefs.personalTime) {
                if (!personal.overlaps(slot)) continue;
                if (personal.start <= slot.start) {
                    blockedUntil = std::max(blockedUntil, personal.end);
                } else {
                    slot.end = std::min(slot.end, personal.start);
                }
            }
            if (blockedUntil >= 0) {
                t = blockedUntil;
                continue;
            }
            // Never offer more in a day than the daily maximum, so any pick of slots obeys it.
            int usedToday = 0;
            for (const auto& s : slots) usedToday += s.day == range.day ? s.length() : 0;
            slot.end = std::min(slot.end, slot.start + prefs.maxStudyMinutesPerDay - usedToday);
            if (prefs.maxStudyMinutesPerDay - usedToday < kShortest) break;
            if (slot.length() >= kShortest) {
                slots.push_back(slot);
                t = slot.end + prefs.breakMinutes;
            } else {
                t = slot.end;
            }
        }
    }
    return slots;
}

std::string GeminiClient::schedulePrompt(const StudyPreferences& prefs,
                                         const std::vector<Assignment>& assignments) {
    Json::Value input;
    input["preferences"] = json::toJson(prefs);
    input["slots"] = Json::arrayValue;
    for (const auto& slot : studySlots(prefs)) {
        Json::Value item;
        item["day"] = dayName(slot.day);
        item["start"] = json::formatClock(slot.start);
        item["end"] = json::formatClock(slot.end);
        input["slots"].append(item);
    }
    input["assignments"] = Json::arrayValue;
    for (const auto& a : assignments) {
        Json::Value item;
        item["id"] = a.id;
        item["title"] = a.title;
        item["dueDay"] = dayName(a.dueDay);
        item["dueTime"] = json::formatClock(a.dueMinute);
        item["estimatedMinutes"] = a.estimatedMinutes;
        input["assignments"].append(item);
    }
    return "Plan this week for the student:\n" + json::write(input);
}

std::vector<StudyBlock> GeminiClient::parseSchedule(const std::string& modelText) {
    const auto parsed = json::parse(stripCodeFence(modelText));
    if (!parsed) throw std::runtime_error("Gemini's answer was not valid JSON");
    const Json::Value& list = parsed->isArray() ? *parsed : (*parsed)["blocks"];
    if (!list.isArray()) throw std::runtime_error("Gemini's answer had no blocks list");

    std::vector<StudyBlock> blocks;
    for (const auto& item : list) {
        std::string error;
        auto block = json::studyBlockFromJson(item, error);
        if (!block) throw std::runtime_error("Gemini returned an unusable block: " + error);
        blocks.push_back(std::move(*block));
    }
    return blocks;
}

std::vector<StudyBlock> GeminiClient::proposeSchedule(const StudyPreferences& prefs,
                                                      const std::vector<Assignment>& assignments) {
    return parseSchedule(generate(kScheduleInstruction, schedulePrompt(prefs, assignments), true));
}

std::string GeminiClient::reply(const std::string& context, const std::string& message) {
    const std::string instruction =
        "You are Cramberry's study assistant. Answer the student's question using only the "
        "tasks and schedule below. Be brief and practical. You can't change their data; if they "
        "ask you to, tell them which page to use. Times are UTC unless a day name is given.\n\n" +
        context;
    return generate(instruction, message, false);
}

Json::Value GeminiClient::requestBody(const GeminiConfig& config, const std::string& systemInstruction,
                                      const std::string& userText, bool wantJson) {
    Json::Value body;
    if (config.api == GeminiConfig::Api::OpenRouter) {
        body["model"] = config.model;
        body["messages"][0]["role"] = "system";
        body["messages"][0]["content"] = systemInstruction;
        body["messages"][1]["role"] = "user";
        body["messages"][1]["content"] = userText;
        body["temperature"] = 0.2;
        // A strict schema makes the provider enforce the reply's shape. Plain
        // {"type":"json_object"} only asks for JSON, and Claude Haiku now and then
        // answered with something that didn't parse, sending the plan to the fallback.
        if (wantJson) body["response_format"] = scheduleResponseFormat();
        if (!config.reasoning) body["reasoning"]["enabled"] = false;
        return body;
    }
    body["systemInstruction"]["parts"][0]["text"] = systemInstruction;
    body["contents"][0]["role"] = "user";
    body["contents"][0]["parts"][0]["text"] = userText;
    body["generationConfig"]["temperature"] = 0.2;
    if (wantJson) body["generationConfig"]["responseMimeType"] = "application/json";
    return body;
}

std::string GeminiClient::replyText(const GeminiConfig& config, const Json::Value& reply) {
    std::string text;
    if (config.api == GeminiConfig::Api::OpenRouter) {
        const auto& content = reply["choices"][0]["message"]["content"];
        if (content.isString()) text = content.asString();
    } else {
        for (const auto& part : reply["candidates"][0]["content"]["parts"]) {
            if (part["text"].isString()) text += part["text"].asString();
        }
    }
    if (text.empty()) throw std::runtime_error("the AI returned no text (blocked or empty answer)");
    return text;
}

std::string GeminiClient::generate(const std::string& systemInstruction, const std::string& userText,
                                   bool wantJson) {
    const bool openRouter = config_.api == GeminiConfig::Api::OpenRouter;
    const char* name = openRouter ? "OpenRouter" : "Gemini";

    auto request = drogon::HttpRequest::newHttpJsonRequest(
        requestBody(config_, systemInstruction, userText, wantJson));
    request->setMethod(drogon::Post);
    if (openRouter) {
        request->setPath("/api/v1/chat/completions");
        request->addHeader("Authorization", "Bearer " + config_.apiKey);
        request->addHeader("X-Title", "Cramberry");
    } else {
        request->setPath("/v1beta/models/" + config_.model + ":generateContent");
        request->addHeader("x-goog-api-key", config_.apiKey);
    }

    const auto client = drogon::HttpClient::newHttpClient(config_.host, loopThread_.getLoop());
    const auto [result, response] = client->sendRequest(request, config_.timeoutSeconds);
    if (result != drogon::ReqResult::Ok || !response) {
        throw std::runtime_error(std::string(name) + " request failed: " + drogon::to_string(result));
    }
    if (response->statusCode() != drogon::k200OK) {
        throw std::runtime_error(std::string(name) + " returned HTTP " +
                                 std::to_string(static_cast<int>(response->statusCode())));
    }
    const auto reply = json::parse(response->body());
    if (!reply) throw std::runtime_error(std::string(name) + " sent a response that isn't JSON");
    return replyText(config_, *reply);
}

}  // namespace cramberry
