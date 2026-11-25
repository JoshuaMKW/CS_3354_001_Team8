#pragma once

// UC-12 Validate AI Response: the AI Integration System flow. Author: Ahnaf Adib (AA).
//
// Follows the AA sequence diagram:
//   generateSchedule(validRequest) -> sendPrompt(tasks, availability, prefs) -> proposedSchedule
//   -> parseResponse(JSON) -> checkFieldsAndFormat(schedule)
//     [output valid]   saveSchedule(studentId, schedule)
//                        [save succeeds] newSchedule
//                        [save fails]    error, existing schedule kept (rolled back)
//     [output invalid] error (AI response rejected), nothing saved
// If Gemini can't be reached, the existing schedule is also kept (NFR-15).
//
// The AI reply must be JSON:
//   {"events": [{"assignmentId": "t-quiz", "title": "Review for Quiz 4",
//                "start": "2026-11-16T18:00", "end": "2026-11-16T19:00"}, ...]}
// "title" is optional; the other fields are required. Times are UTC.

#include <cstddef>
#include <string>
#include <vector>

#include "schedule/SchedulingTypes.h"

namespace cramberry {

// parseResponse() and checkFieldsAndFormat().
struct AIValidationResult {
    bool valid = false;
    std::vector<ScheduledEvent> events;  // earliest first; empty unless valid
    std::vector<std::string> reasons;    // every problem found; empty if valid
};

constexpr std::size_t kMaxAiEvents = 500;

// Valid means: the JSON parses, every event has the right fields and types, every event ends after
// it starts, nothing starts in the past, every event belongs to a task in the request and ends
// before that task is due, no two events overlap, and every task gets at least one event.
AIValidationResult validateAIResponse(const std::string& rawResponse,
                                      const PreparedSchedulingRequest& request);

// Wraps the Gemini API call. Throws std::runtime_error if Gemini can't be reached or times out.
class IGeminiClient {
public:
    virtual ~IGeminiClient() = default;
    virtual std::string sendPrompt(const PreparedSchedulingRequest& request) = 0;
};

// Where accepted schedules are stored (Supabase in production).
class IGeneratedScheduleRepository {
public:
    virtual ~IGeneratedScheduleRepository() = default;
    virtual std::vector<ScheduledEvent> load(const std::string& studentId) const = 0;
    // Replaces the student's schedule in one transaction. Returns false if it rolled back; the
    // previous schedule must then be unchanged.
    virtual bool saveSchedule(const std::string& studentId, const std::vector<ScheduledEvent>& events) = 0;
};

struct GenerationResult {
    enum class Status { Saved, Rejected, SaveFailed, AiUnavailable };

    Status status = Status::AiUnavailable;
    std::string message;
    std::vector<ScheduledEvent> schedule;  // the new schedule, when status == Saved
    std::vector<std::string> reasons;      // why the AI's reply was rejected
};

class ScheduleGenerationService {
public:
    ScheduleGenerationService(IGeminiClient& gemini, IGeneratedScheduleRepository& schedules);

    GenerationResult generateSchedule(const PreparedSchedulingRequest& request);

private:
    IGeminiClient& gemini_;
    IGeneratedScheduleRepository& schedules_;
};

}  // namespace cramberry
