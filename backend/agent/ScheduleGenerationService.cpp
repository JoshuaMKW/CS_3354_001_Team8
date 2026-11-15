// UC-12 Validate AI Response: the AI Integration System flow. Author: Ahnaf Adib (AA).
// See ScheduleGenerationService.h for the flow this implements.

#include "agent/ScheduleGenerationService.h"

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

namespace cramberry {

namespace {

using nlohmann::json;

// Gemini often wraps JSON in a Markdown code fence (```json ... ```). Return what's inside it.
std::string stripCodeFence(const std::string& raw) {
    const auto first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || raw.compare(first, 3, "```") != 0) return raw;
    const auto bodyStart = raw.find('\n', first);
    const auto fenceEnd = raw.rfind("```");
    if (bodyStart == std::string::npos || fenceEnd <= bodyStart) return raw;
    return raw.substr(bodyStart + 1, fenceEnd - bodyStart - 1);
}

std::string eventName(std::size_t index) { return "Event " + std::to_string(index + 1); }

// Reads a string field. Records a reason and returns nothing if it is missing (when required) or
// isn't text.
std::optional<std::string> readText(const json& event, const std::string& key, std::size_t index,
                                    bool required, std::vector<std::string>& reasons) {
    if (!event.contains(key)) {
        if (required) reasons.push_back(eventName(index) + " is missing \"" + key + "\"");
        return std::nullopt;
    }
    if (!event.at(key).is_string()) {
        reasons.push_back(eventName(index) + ": \"" + key + "\" must be text");
        return std::nullopt;
    }
    return event.at(key).get<std::string>();
}

}  // namespace

AIValidationResult validateAIResponse(const std::string& rawResponse,
                                      const PreparedSchedulingRequest& request) {
    AIValidationResult result;
    std::vector<std::string>& reasons = result.reasons;

    // parseResponse(JSON)
    const json doc = json::parse(stripCodeFence(rawResponse), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        reasons.push_back("AI response is not valid JSON");
        return result;
    }
    if (!doc.is_object() || !doc.contains("events") || !doc.at("events").is_array()) {
        reasons.push_back("AI response is missing the \"events\" list");
        return result;
    }
    const json& events = doc.at("events");
    if (events.empty()) {
        reasons.push_back("AI response has no study events");
        return result;
    }
    if (events.size() > kMaxAiEvents) {
        reasons.push_back("AI response has more than " + std::to_string(kMaxAiEvents) + " events");
        return result;
    }

    // checkFieldsAndFormat(schedule)
    std::map<std::string, const TaskDetails*> tasks;
    for (const auto& task : request.tasks) tasks[task.id] = &task;

    std::vector<std::pair<std::size_t, ScheduledEvent>> timed;  // events with usable times
    std::set<std::string> scheduledTasks;

    for (std::size_t i = 0; i < events.size(); ++i) {
        const json& event = events[i];
        if (!event.is_object()) {
            reasons.push_back(eventName(i) + " is not an object");
            continue;
        }
        const auto assignmentId = readText(event, "assignmentId", i, true, reasons);
        const auto title = readText(event, "title", i, false, reasons);
        const auto startText = readText(event, "start", i, true, reasons);
        const auto endText = readText(event, "end", i, true, reasons);

        std::optional<Timestamp> start;
        std::optional<Timestamp> end;
        if (startText && !(start = parseIsoMinute(*startText))) {
            reasons.push_back(eventName(i) + " has an invalid start time \"" + *startText + "\"");
        }
        if (endText && !(end = parseIsoMinute(*endText))) {
            reasons.push_back(eventName(i) + " has an invalid end time \"" + *endText + "\"");
        }

        const TaskDetails* task = nullptr;
        if (assignmentId) {
            const auto it = tasks.find(*assignmentId);
            if (it == tasks.end()) {
                reasons.push_back(eventName(i) + " refers to unknown assignment \"" + *assignmentId + "\"");
            } else {
                task = it->second;
                scheduledTasks.insert(task->id);
            }
        }

        if (!start || !end) continue;
        if (*end <= *start) {
            reasons.push_back(eventName(i) + " ends before it starts");
            continue;
        }
        if (*start < request.submittedAt) {
            reasons.push_back(eventName(i) + " starts in the past");
        }
        if (task && *end > task->dueUtc) {
            reasons.push_back(eventName(i) + " ends after \"" + task->title + "\" is due");
        }
        timed.emplace_back(i, ScheduledEvent{assignmentId.value_or(""),
                                             title.value_or(task ? task->title : ""), *start, *end});
    }

    // No two events may overlap. Sweep in start order, remembering the event that ends last.
    std::sort(timed.begin(), timed.end(), [](const auto& a, const auto& b) {
        return a.second.start != b.second.start ? a.second.start < b.second.start : a.first < b.first;
    });
    for (std::size_t k = 1, latest = 0; k < timed.size(); ++k) {
        if (timed[k].second.start < timed[latest].second.end) {
            reasons.push_back(eventName(timed[k].first) + " overlaps " +
                              "event " + std::to_string(timed[latest].first + 1));
        }
        if (timed[k].second.end > timed[latest].second.end) latest = k;
    }

    for (const auto& task : request.tasks) {
        if (!scheduledTasks.contains(task.id)) {
            reasons.push_back("\"" + task.title + "\" is not scheduled");
        }
    }

    if (reasons.empty()) {
        result.valid = true;
        for (auto& [index, event] : timed) result.events.push_back(std::move(event));
    }
    return result;
}

ScheduleGenerationService::ScheduleGenerationService(IGeminiClient& gemini,
                                                     IGeneratedScheduleRepository& schedules)
    : gemini_(gemini), schedules_(schedules) {}

GenerationResult ScheduleGenerationService::generateSchedule(const PreparedSchedulingRequest& request) {
    using Status = GenerationResult::Status;

    // sendPrompt(tasks, availability, prefs) -> proposedSchedule (JSON)
    std::string reply;
    try {
        reply = gemini_.sendPrompt(request);
    } catch (const std::exception&) {
        return {Status::AiUnavailable,
                "The AI service is unavailable right now. Your existing schedule was kept.", {}, {}};
    }

    // parseResponse(JSON) -> checkFieldsAndFormat(schedule)
    AIValidationResult checked = validateAIResponse(reply, request);
    if (!checked.valid) {
        return {Status::Rejected,
                "The AI returned an invalid schedule, so it wasn't saved. Please try again.", {},
                std::move(checked.reasons)};
    }

    // saveSchedule(studentId, schedule)
    bool saved = false;
    try {
        saved = schedules_.saveSchedule(request.studentId, checked.events);
    } catch (const std::exception&) {
        saved = false;
    }
    if (!saved) {
        return {Status::SaveFailed,
                "Couldn't save your new schedule. Your existing schedule was kept.", {}, {}};
    }
    return {Status::Saved, "Your new schedule is ready.", std::move(checked.events), {}};
}

}  // namespace cramberry
