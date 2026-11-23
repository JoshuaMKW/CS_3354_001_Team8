#include "tasks/TaskController.h"

#include "user/Crypto.h"

namespace cramberry {

namespace {

constexpr Timestamp kWeekMinutes = 7 * 24 * 60;

void syncCompletion(Task& task, std::optional<Timestamp> previousCompletion, Timestamp now) {
    if (task.status == TaskStatus::Completed) {
        task.completedAt = previousCompletion.value_or(now);
    } else {
        task.completedAt.reset();
    }
}

}  // namespace

std::optional<std::string> TaskController::validate(const CourseInput& input) {
    if (input.name.empty() && input.code.empty()) return "Enter a course name or code";
    if (input.name.size() > 120 || input.code.size() > 30 || input.term.size() > 30) {
        return "Course name, code or term is too long";
    }
    return std::nullopt;
}

std::optional<std::string> TaskController::validate(const Task& task) {
    if (task.title.empty()) return "Enter a title";
    if (task.title.size() > 200) return "Title must be 200 characters or fewer";
    if (task.notes.size() > 4000) return "Notes must be 4000 characters or fewer";
    if (task.dueAt <= 0) return "Enter a due date";
    if (task.estimatedMinutes < 1 || task.estimatedMinutes > kMaxEstimateMinutes) {
        return "Estimated time must be between 1 minute and 7 days";
    }
    if (task.priority < 1 || task.priority > 3) return "Priority must be 1 (high), 2 (medium) or 3 (low)";
    return std::nullopt;
}

std::optional<Error> TaskController::checkCourse(const std::string& studentId,
                                                 const std::string& courseId) const {
    if (courseId.empty() || tasks_.findCourse(studentId, courseId)) return std::nullopt;
    return invalid("That course doesn't exist");
}

std::vector<Course> TaskController::listCourses(const std::string& studentId) const {
    return tasks_.listCourses(studentId);
}

Outcome<Course> TaskController::createCourse(const std::string& studentId, const CourseInput& input) {
    if (auto e = validate(input)) return invalid(*e);
    Course course{crypto::newId(), studentId, input.name, input.code, input.term};
    tasks_.saveCourse(course);
    return course;
}

Outcome<Course> TaskController::updateCourse(const std::string& studentId, const std::string& id,
                                             const CourseInput& input) {
    if (!tasks_.findCourse(studentId, id)) return notFound("Course not found");
    if (auto e = validate(input)) return invalid(*e);
    Course course{id, studentId, input.name, input.code, input.term};
    tasks_.saveCourse(course);
    return course;
}

Outcome<Done> TaskController::deleteCourse(const std::string& studentId, const std::string& id) {
    if (!tasks_.deleteCourse(studentId, id)) return notFound("Course not found");
    return Done{};
}

std::vector<Task> TaskController::listTasks(const std::string& studentId, const TaskFilter& filter,
                                            bool overdueOnly, Timestamp now) const {
    auto tasks = tasks_.listTasks(studentId, filter);
    if (overdueOnly) {
        std::erase_if(tasks, [now](const Task& t) { return !t.isOverdue(now); });
    }
    return tasks;
}

Outcome<Task> TaskController::getTask(const std::string& studentId, const std::string& id) const {
    auto task = tasks_.findTask(studentId, id);
    if (!task) return notFound("Task not found");
    return *task;
}

Outcome<Task> TaskController::createTask(const std::string& studentId, Task draft, Timestamp now) {
    draft.id = crypto::newId();
    draft.studentId = studentId;
    draft.createdAt = now;
    syncCompletion(draft, std::nullopt, now);
    if (auto e = validate(draft)) return invalid(*e);
    if (auto e = checkCourse(studentId, draft.courseId)) return *e;
    tasks_.saveTask(draft);
    return draft;
}

Outcome<Task> TaskController::updateTask(const std::string& studentId, Task edited, Timestamp now) {
    const auto existing = tasks_.findTask(studentId, edited.id);
    if (!existing) return notFound("Task not found");
    edited.studentId = studentId;
    edited.createdAt = existing->createdAt;
    syncCompletion(edited, existing->completedAt, now);
    if (auto e = validate(edited)) return invalid(*e);
    if (auto e = checkCourse(studentId, edited.courseId)) return *e;
    tasks_.saveTask(edited);
    return edited;
}

Outcome<Task> TaskController::setStatus(const std::string& studentId, const std::string& id,
                                        TaskStatus status, Timestamp now) {
    auto task = tasks_.findTask(studentId, id);
    if (!task) return notFound("Task not found");
    const auto previous = task->completedAt;
    task->status = status;
    syncCompletion(*task, previous, now);
    tasks_.saveTask(*task);
    return *task;
}

Outcome<Done> TaskController::deleteTask(const std::string& studentId, const std::string& id) {
    if (!tasks_.deleteTask(studentId, id)) return notFound("Task not found");
    return Done{};
}

Outcome<WeeklyReport> TaskController::weeklyReport(const std::string& studentId,
                                                   Timestamp weekStart, Timestamp now) const {
    WeeklyReport report;
    report.weekStart = weekStart;
    report.weekEnd = weekStart + kWeekMinutes;

    Timestamp totalCompletionMinutes = 0;
    for (const auto& task : tasks_.listTasks(studentId, {})) {
        if (task.completedAt) {
            if (*task.completedAt >= report.weekStart && *task.completedAt < report.weekEnd) {
                report.completed.push_back(task);
                totalCompletionMinutes += *task.completedAt - task.createdAt;
            }
            continue;
        }
        if (task.dueAt < report.weekStart || task.dueAt >= report.weekEnd) continue;
        if (task.dueAt < now) {
            report.overdue.push_back(task);
        } else {
            report.remaining.push_back(task);
        }
    }
    if (!report.completed.empty()) {
        report.averageCompletionMinutes =
            static_cast<int>(totalCompletionMinutes / static_cast<Timestamp>(report.completed.size()));
    }
    return report;
}

}  // namespace cramberry
