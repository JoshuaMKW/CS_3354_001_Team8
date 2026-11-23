#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/Outcome.h"
#include "tasks/Task.h"

namespace cramberry {

struct CourseInput {
    std::string name;
    std::string code;
    std::string term;
};

// UC-14 View Weekly Progress Report.
struct WeeklyReport {
    Timestamp weekStart = 0;
    Timestamp weekEnd = 0;
    std::vector<Task> completed;  // completed during the week
    std::vector<Task> overdue;    // due during the week, before now, and still not done
    std::vector<Task> remaining;  // due later this week, not done yet
    // FR-30: average minutes from creating a task to completing it, for tasks
    // completed this week. Empty when nothing was completed.
    std::optional<int> averageCompletionMinutes;
};

// Academic task management from the SOW: courses, assignments, exams and other
// tasks; edit, delete, status changes (UC-13), filters (FR-24), overdue (FR-26).
class TaskController {
public:
    static constexpr int kMaxEstimateMinutes = 7 * 24 * 60;

    explicit TaskController(ITaskRepository& tasks) : tasks_(tasks) {}

    std::vector<Course> listCourses(const std::string& studentId) const;
    Outcome<Course> createCourse(const std::string& studentId, const CourseInput& input);
    Outcome<Course> updateCourse(const std::string& studentId, const std::string& id,
                                 const CourseInput& input);
    Outcome<Done> deleteCourse(const std::string& studentId, const std::string& id);

    std::vector<Task> listTasks(const std::string& studentId, const TaskFilter& filter,
                                bool overdueOnly, Timestamp now) const;
    Outcome<Task> getTask(const std::string& studentId, const std::string& id) const;
    // Fills in id, studentId, createdAt and completedAt; validates the rest.
    Outcome<Task> createTask(const std::string& studentId, Task draft, Timestamp now);
    // Saves an edited copy of an existing task (fetch it with getTask first).
    Outcome<Task> updateTask(const std::string& studentId, Task edited, Timestamp now);
    Outcome<Task> setStatus(const std::string& studentId, const std::string& id, TaskStatus status,
                            Timestamp now);
    Outcome<Done> deleteTask(const std::string& studentId, const std::string& id);

    Outcome<WeeklyReport> weeklyReport(const std::string& studentId, Timestamp weekStart,
                                       Timestamp now) const;

    static std::optional<std::string> validate(const CourseInput& input);
    static std::optional<std::string> validate(const Task& task);

private:
    std::optional<Error> checkCourse(const std::string& studentId, const std::string& courseId) const;

    ITaskRepository& tasks_;
};

}  // namespace cramberry
