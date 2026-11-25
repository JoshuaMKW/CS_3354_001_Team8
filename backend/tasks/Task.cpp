#include "tasks/Task.h"

namespace cramberry {

std::string toString(TaskStatus status) {
    switch (status) {
        case TaskStatus::NotStarted: return "NOT_STARTED";
        case TaskStatus::InProgress: return "IN_PROGRESS";
        case TaskStatus::Completed: return "COMPLETED";
    }
    return "NOT_STARTED";
}

std::optional<TaskStatus> parseTaskStatus(std::string_view text) {
    if (text == "NOT_STARTED") return TaskStatus::NotStarted;
    if (text == "IN_PROGRESS") return TaskStatus::InProgress;
    if (text == "COMPLETED") return TaskStatus::Completed;
    return std::nullopt;
}

std::string toString(TaskType type) {
    switch (type) {
        case TaskType::Assignment: return "assignment";
        case TaskType::Exam: return "exam";
        case TaskType::Study: return "study";
        case TaskType::Other: return "other";
    }
    return "other";
}

std::optional<TaskType> parseTaskType(std::string_view text) {
    if (text == "assignment") return TaskType::Assignment;
    if (text == "exam") return TaskType::Exam;
    if (text == "study") return TaskType::Study;
    if (text == "other") return TaskType::Other;
    return std::nullopt;
}

}  // namespace cramberry
