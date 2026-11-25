#include "agent/GeminiClient.h"

#include <stdexcept>

#include "common/Json.h"

namespace cramberry {

namespace {

constexpr const char* kScheduleInstruction =
    "You are the scheduling engine of Cramberry, a student study planner. "
    "Build a one-week study schedule (Monday to Sunday, the student's local time) that gives "
    "every assignment enough study time before it is due. Follow every rule exactly; a schedule "
    "that breaks any rule is thrown away:\n"
    "1. Every block lies completely inside one of the studyHours ranges on the same day.\n"
    "2. No block overlaps any personalTime range.\n"
    "3. No block is longer than sessionMinutes.\n"
    "4. The blocks on one day add up to at most maxStudyMinutesPerDay.\n"
    "5. Blocks on the same day are at least breakMinutes apart and never overlap.\n"
    "6. Every assignment gets at least one block, and its blocks end before its due day and time.\n"
    "7. Aim for each assignment's estimatedMinutes in total; schedule earlier deadlines first.\n"
    "8. Use the assignment's exact id as assignmentId and its title as title.\n"
    "Times are 24-hour \"HH:MM\". Days are Mon, Tue, Wed, Thu, Fri, Sat, Sun. "
    "Reply with JSON only, in this shape: "
    "{\"blocks\":[{\"assignmentId\":\"...\",\"title\":\"...\",\"day\":\"Mon\",\"start\":\"16:00\",\"end\":\"17:00\"}]}";

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
    client_ = drogon::HttpClient::newHttpClient(config_.host, loopThread_.getLoop());
}

std::string GeminiClient::schedulePrompt(const StudyPreferences& prefs,
                                         const std::vector<Assignment>& assignments) {
    Json::Value input;
    input["preferences"] = json::toJson(prefs);
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

std::string GeminiClient::generate(const std::string& systemInstruction, const std::string& userText,
                                   bool wantJson) {
    Json::Value body;
    body["systemInstruction"]["parts"][0]["text"] = systemInstruction;
    body["contents"][0]["role"] = "user";
    body["contents"][0]["parts"][0]["text"] = userText;
    body["generationConfig"]["temperature"] = 0.2;
    if (wantJson) body["generationConfig"]["responseMimeType"] = "application/json";

    auto request = drogon::HttpRequest::newHttpJsonRequest(body);
    request->setMethod(drogon::Post);
    request->setPath("/v1beta/models/" + config_.model + ":generateContent");
    request->addHeader("x-goog-api-key", config_.apiKey);

    const auto [result, response] = client_->sendRequest(request, config_.timeoutSeconds);
    if (result != drogon::ReqResult::Ok || !response) {
        throw std::runtime_error("Gemini request failed: " + drogon::to_string(result));
    }
    if (response->statusCode() != drogon::k200OK) {
        throw std::runtime_error("Gemini returned HTTP " +
                                 std::to_string(static_cast<int>(response->statusCode())));
    }
    const auto reply = json::parse(response->body());
    if (!reply) throw std::runtime_error("Gemini sent a response that isn't JSON");

    std::string text;
    for (const auto& part : (*reply)["candidates"][0]["content"]["parts"]) {
        if (part["text"].isString()) text += part["text"].asString();
    }
    if (text.empty()) throw std::runtime_error("Gemini returned no text (blocked or empty answer)");
    return text;
}

}  // namespace cramberry
