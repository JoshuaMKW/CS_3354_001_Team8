#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/Time.h"

namespace cramberry {

// TaskStatus in the UML class diagram (FR-23, UC-13).
enum class TaskStatus { NotStarted, InProgress, Completed };
enum class TaskType { Assignment, Exam, Study, Other };

std::string toString(TaskStatus status);
std::optional<TaskStatus> parseTaskStatus(std::string_view text);
std::string toString(TaskType type);
std::optional<TaskType> parseTaskType(std::string_view text);

struct Course {
    std::string id;
    std::string studentId;
    std::string name;  // "Software Engineering"
    std::string code;  // "CS 3354"
    std::string term;  // "Fall 2026"
};

// Assignment in the UML class diagram, widened to cover exams and other academic tasks.
struct Task {
    std::string id;
    std::string studentId;
    std::string courseId;  // empty when the task isn't tied to a course
    std::string title;
    std::string notes;
    TaskType type = TaskType::Assignment;
    Timestamp dueAt = 0;
    int estimatedMinutes = 60;
    int priority = 2;  // 1 = high, 2 = medium, 3 = low
    TaskStatus status = TaskStatus::NotStarted;
    std::optional<Timestamp> completedAt;
    Timestamp createdAt = 0;

    bool isOverdue(Timestamp now) const { return status != TaskStatus::Completed && dueAt < now; }
};

// FR-24: filter by course or date.
struct TaskFilter {
    std::optional<std::string> courseId;
    std::optional<Timestamp> dueFrom;  // inclusive
    std::optional<Timestamp> dueTo;    // exclusive
    std::optional<TaskStatus> status;
};

class ITaskRepository {
public:
    virtual ~ITaskRepository() = default;

    virtual std::vector<Course> listCourses(const std::string& studentId) const = 0;
    virtual std::optional<Course> findCourse(const std::string& studentId, const std::string& id) const = 0;
    virtual void saveCourse(const Course& course) = 0;  // insert or update
    // Returns false if the student has no such course. Tasks keep existing, unlinked.
    virtual bool deleteCourse(const std::string& studentId, const std::string& id) = 0;

    // Sorted by due date, earliest first.
    virtual std::vector<Task> listTasks(const std::string& studentId, const TaskFilter& filter) const = 0;
    virtual std::optional<Task> findTask(const std::string& studentId, const std::string& id) const = 0;
    virtual void saveTask(const Task& task) = 0;  // insert or update
    virtual bool deleteTask(const std::string& studentId, const std::string& id) = 0;
};

}  // namespace cramberry
