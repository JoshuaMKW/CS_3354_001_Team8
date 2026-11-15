#pragma once

// UC-10 Process Scheduling Request. Author: Ahnaf Adib (AA).
//
// The Backend API System's handler for POST /api/schedule (FR-27). It follows the AA sequence
// diagram:
//   validate(fields, dates, types)
//     [input valid]   getTaskDetails(taskIds) -> prepareRequest(tasks, prefs)
//                     -> forwardRequest(validRequest) -> 202 Accepted(requestId)
//     [input invalid] 400 Bad Request(fieldErrors)

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "schedule/SchedulingTypes.h"
#include "settings/Settings.h"

namespace cramberry {

// One task in the request body, exactly as the web app sends it.
struct TaskInput {
    std::string taskId;
    std::string dueDate;  // "YYYY-MM-DDTHH:MM", UTC
    int estimatedMinutes = 0;
};

struct SchedulingRequestInput {
    std::vector<TaskInput> tasks;
    StudyPreferences prefs;
};

// A problem with one field, shown next to that field in the web app.
struct FieldError {
    std::string field;  // e.g. "tasks[0].dueDate"
    std::string message;

    bool operator==(const FieldError&) const = default;
};

// Task Management System.
class ITaskDetailsSource {
public:
    virtual ~ITaskDetailsSource() = default;
    // Returns the tasks with these IDs that belong to studentId. IDs that don't exist, or that
    // belong to another student, are left out.
    virtual std::vector<TaskDetails> getTaskDetails(const std::string& studentId,
                                                    const std::vector<std::string>& taskIds) const = 0;
};

// Schedule System, which hands the request to the AI Integration System (UC-12).
class IScheduleSystem {
public:
    virtual ~IScheduleSystem() = default;
    // Queues the request and returns its ID, or nothing if the Schedule System is unavailable.
    virtual std::optional<std::string> forwardRequest(const PreparedSchedulingRequest& request) = 0;
};

struct ProcessResult {
    int httpStatus = 500;  // 202, 400, 401 or 503
    std::string message;
    std::string requestId;                // set when httpStatus == 202
    std::vector<FieldError> fieldErrors;  // set when httpStatus == 400
};

class SchedulingRequestController {
public:
    static constexpr std::size_t kMaxTasksPerRequest = 50;
    static constexpr int kMaxEstimatedMinutes = 100 * 60;

    SchedulingRequestController(ITaskDetailsSource& tasks, IScheduleSystem& scheduler);

    // studentId comes from the verified Supabase Auth session, never from the request body.
    ProcessResult processRequest(const std::string& studentId, const SchedulingRequestInput& input,
                                 Timestamp now);

    // validate(fields, dates, types): every problem in the request, in field order.
    static std::vector<FieldError> validate(const SchedulingRequestInput& input, Timestamp now);

    // prepareRequest(tasks, prefs): applies the student's latest deadlines and estimates to the
    // stored task details and orders the tasks by deadline, earliest first.
    static PreparedSchedulingRequest prepareRequest(const std::string& studentId,
                                                    const SchedulingRequestInput& input,
                                                    std::vector<TaskDetails> details, Timestamp now);

private:
    ITaskDetailsSource& tasks_;
    IScheduleSystem& scheduler_;
};

}  // namespace cramberry
