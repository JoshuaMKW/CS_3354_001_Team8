// UC-10 Process Scheduling Request. Author: Ahnaf Adib (AA).
// See SchedulingRequestController.h for the flow this implements.

#include "api/SchedulingRequestController.h"

#include <algorithm>
#include <map>
#include <set>

#include "schedule/SchedulingTypes.h"
#include "settings/SettingsController.h"

namespace cramberry {

namespace {

const char* const kFixFields = "Please fix the highlighted fields.";

std::string taskField(std::size_t index, const std::string& name) {
    return "tasks[" + std::to_string(index) + "]." + name;
}

}  // namespace

SchedulingRequestController::SchedulingRequestController(ITaskDetailsSource& tasks,
                                                         IScheduleSystem& scheduler)
    : tasks_(tasks), scheduler_(scheduler) {}

std::vector<FieldError> SchedulingRequestController::validate(const SchedulingRequestInput& input,
                                                              Timestamp now) {
    std::vector<FieldError> errors;
    if (input.tasks.empty()) {
        errors.push_back({"tasks", "Add at least one task to schedule"});
    }
    if (input.tasks.size() > kMaxTasksPerRequest) {
        errors.push_back({"tasks", "You can schedule at most " +
                                       std::to_string(kMaxTasksPerRequest) + " tasks at once"});
    }

    std::set<std::string> seen;
    for (std::size_t i = 0; i < input.tasks.size(); ++i) {
        const TaskInput& task = input.tasks[i];

        if (task.taskId.empty()) {
            errors.push_back({taskField(i, "taskId"), "Task ID is required"});
        } else if (!seen.insert(task.taskId).second) {
            errors.push_back({taskField(i, "taskId"), "Task " + task.taskId + " is listed more than once"});
        }

        if (task.dueDate.empty()) {
            errors.push_back({taskField(i, "dueDate"), "Due date is required"});
        } else if (const auto due = parseIsoMinute(task.dueDate); !due) {
            errors.push_back({taskField(i, "dueDate"), "Due date must look like 2026-11-20T23:59"});
        } else if (*due <= now) {
            errors.push_back({taskField(i, "dueDate"), "Due date must be in the future"});
        }

        if (task.estimatedMinutes <= 0) {
            errors.push_back({taskField(i, "estimatedMinutes"), "Estimated time must be greater than 0 min"});
        } else if (task.estimatedMinutes > kMaxEstimatedMinutes) {
            errors.push_back({taskField(i, "estimatedMinutes"), "Estimated time can't be more than 100 hours"});
        }
    }

    if (input.prefs.studyHours.empty()) {
        errors.push_back({"prefs.studyHours", "Add at least one block of study hours"});
    }
    // Same rules as UC-15 Manage Study Preferences, so both screens agree.
    if (auto error = SettingsController::validate(input.prefs)) {
        errors.push_back({"prefs", *error});
    }
    return errors;
}

PreparedSchedulingRequest SchedulingRequestController::prepareRequest(
    const std::string& studentId, const SchedulingRequestInput& input,
    std::vector<TaskDetails> details, Timestamp now) {
    std::map<std::string, const TaskInput*> byId;
    for (const auto& task : input.tasks) byId[task.taskId] = &task;

    for (auto& task : details) {
        const auto it = byId.find(task.id);
        if (it == byId.end()) continue;
        // The deadline and estimate the student just submitted win over the stored values.
        if (const auto due = parseIsoMinute(it->second->dueDate)) task.dueUtc = *due;
        task.estimatedMinutes = it->second->estimatedMinutes;
    }
    std::sort(details.begin(), details.end(), [](const TaskDetails& a, const TaskDetails& b) {
        return a.dueUtc != b.dueUtc ? a.dueUtc < b.dueUtc : a.id < b.id;
    });
    return {studentId, std::move(details), input.prefs, now};
}

ProcessResult SchedulingRequestController::processRequest(const std::string& studentId,
                                                          const SchedulingRequestInput& input,
                                                          Timestamp now) {
    if (studentId.empty()) {
        return {401, "Please sign in to build a schedule.", {}, {}};
    }

    // validate(fields, dates, types)
    std::vector<FieldError> errors = validate(input, now);
    if (!errors.empty()) {
        return {400, kFixFields, {}, std::move(errors)};
    }

    // getTaskDetails(taskIds)
    std::vector<std::string> ids;
    ids.reserve(input.tasks.size());
    for (const auto& task : input.tasks) ids.push_back(task.taskId);

    std::map<std::string, TaskDetails> found;
    for (auto& task : tasks_.getTaskDetails(studentId, ids)) {
        // Never trust a row that belongs to someone else, even if the source returned it (NFR-19).
        if (task.studentId == studentId) found.emplace(task.id, std::move(task));
    }

    std::vector<TaskDetails> details;
    for (std::size_t i = 0; i < input.tasks.size(); ++i) {
        const auto it = found.find(input.tasks[i].taskId);
        if (it == found.end()) {
            errors.push_back({taskField(i, "taskId"), "Task " + input.tasks[i].taskId + " was not found"});
        } else if (it->second.completed) {
            errors.push_back({taskField(i, "taskId"), "\"" + it->second.title + "\" is already completed"});
        } else {
            details.push_back(it->second);
        }
    }
    if (!errors.empty()) {
        return {400, kFixFields, {}, std::move(errors)};
    }

    // prepareRequest(tasks, prefs) -> forwardRequest(validRequest)
    const auto requestId =
        scheduler_.forwardRequest(prepareRequest(studentId, input, std::move(details), now));
    if (!requestId) {
        return {503, "The scheduler is busy right now. Please try again in a moment.", {}, {}};
    }
    return {202, "Building your schedule", *requestId, {}};
}

}  // namespace cramberry
