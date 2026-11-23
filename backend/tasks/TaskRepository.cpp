#include "tasks/TaskRepository.h"

#include <algorithm>

namespace cramberry {

namespace {

constexpr const char* kTaskColumns =
    "id, student_id, course_id, title, notes, kind, due_at, estimated_minutes, priority, status, "
    "completed_at, created_at";

Course toCourse(const drogon::orm::Row& row) {
    return {row["id"].as<std::string>(), row["student_id"].as<std::string>(),
            row["name"].as<std::string>(), row["code"].as<std::string>(),
            row["term"].as<std::string>()};
}

Task toTask(const drogon::orm::Row& row) {
    Task t;
    t.id = row["id"].as<std::string>();
    t.studentId = row["student_id"].as<std::string>();
    t.courseId = row["course_id"].as<std::string>();
    t.title = row["title"].as<std::string>();
    t.notes = row["notes"].as<std::string>();
    t.type = parseTaskType(row["kind"].as<std::string>()).value_or(TaskType::Other);
    t.dueAt = row["due_at"].as<std::int64_t>();
    t.estimatedMinutes = row["estimated_minutes"].as<int>();
    t.priority = row["priority"].as<int>();
    t.status = parseTaskStatus(row["status"].as<std::string>()).value_or(TaskStatus::NotStarted);
    const auto completedAt = row["completed_at"].as<std::int64_t>();
    if (completedAt != 0) t.completedAt = completedAt;
    t.createdAt = row["created_at"].as<std::int64_t>();
    return t;
}

}  // namespace

std::vector<Course> SqlTaskRepository::listCourses(const std::string& studentId) const {
    std::vector<Course> courses;
    for (const auto& row : db_.exec(
             "SELECT id, student_id, name, code, term FROM courses WHERE student_id = $1 "
             "ORDER BY code, name",
             studentId)) {
        courses.push_back(toCourse(row));
    }
    return courses;
}

std::optional<Course> SqlTaskRepository::findCourse(const std::string& studentId,
                                                    const std::string& id) const {
    const auto rows = db_.exec(
        "SELECT id, student_id, name, code, term FROM courses WHERE student_id = $1 AND id = $2",
        studentId, id);
    if (rows.empty()) return std::nullopt;
    return toCourse(rows[0]);
}

void SqlTaskRepository::saveCourse(const Course& c) {
    db_.exec(
        "INSERT INTO courses (id, student_id, name, code, term) VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (id) DO UPDATE SET name = excluded.name, code = excluded.code, "
        "term = excluded.term WHERE courses.student_id = excluded.student_id",
        c.id, c.studentId, c.name, c.code, c.term);
}

bool SqlTaskRepository::deleteCourse(const std::string& studentId, const std::string& id) {
    auto tx = db_.begin();
    tx.exec("UPDATE tasks SET course_id = '' WHERE student_id = $1 AND course_id = $2", studentId, id);
    const auto deleted =
        tx.exec("DELETE FROM courses WHERE student_id = $1 AND id = $2", studentId, id).affectedRows();
    tx.commit();
    return deleted > 0;
}

std::vector<Task> SqlTaskRepository::listTasks(const std::string& studentId,
                                               const TaskFilter& filter) const {
    std::vector<Task> tasks;
    for (const auto& row : db_.exec(std::string("SELECT ") + kTaskColumns +
                                        " FROM tasks WHERE student_id = $1 ORDER BY due_at, title",
                                    studentId)) {
        Task t = toTask(row);
        if (filter.courseId && t.courseId != *filter.courseId) continue;
        if (filter.dueFrom && t.dueAt < *filter.dueFrom) continue;
        if (filter.dueTo && t.dueAt >= *filter.dueTo) continue;
        if (filter.status && t.status != *filter.status) continue;
        tasks.push_back(std::move(t));
    }
    return tasks;
}

std::optional<Task> SqlTaskRepository::findTask(const std::string& studentId,
                                                const std::string& id) const {
    const auto rows = db_.exec(std::string("SELECT ") + kTaskColumns +
                                   " FROM tasks WHERE student_id = $1 AND id = $2",
                               studentId, id);
    if (rows.empty()) return std::nullopt;
    return toTask(rows[0]);
}

void SqlTaskRepository::saveTask(const Task& t) {
    db_.exec(
        "INSERT INTO tasks (id, student_id, course_id, title, notes, kind, due_at, "
        "estimated_minutes, priority, status, completed_at, created_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12) "
        "ON CONFLICT (id) DO UPDATE SET course_id = excluded.course_id, title = excluded.title, "
        "notes = excluded.notes, kind = excluded.kind, due_at = excluded.due_at, "
        "estimated_minutes = excluded.estimated_minutes, priority = excluded.priority, "
        "status = excluded.status, completed_at = excluded.completed_at "
        "WHERE tasks.student_id = excluded.student_id",
        t.id, t.studentId, t.courseId, t.title, t.notes, toString(t.type),
        static_cast<std::int64_t>(t.dueAt), t.estimatedMinutes, t.priority, toString(t.status),
        static_cast<std::int64_t>(t.completedAt.value_or(0)), static_cast<std::int64_t>(t.createdAt));
}

bool SqlTaskRepository::deleteTask(const std::string& studentId, const std::string& id) {
    return db_.exec("DELETE FROM tasks WHERE student_id = $1 AND id = $2", studentId, id)
               .affectedRows() > 0;
}

std::vector<DeadlineItem> SqlDeadlineSource::upcomingDeadlines(Timestamp now) const {
    std::vector<DeadlineItem> items;
    for (const auto& row : db_.exec(
             "SELECT id, student_id, title, due_at FROM tasks "
             "WHERE status <> 'COMPLETED' AND due_at > $1 AND due_at <= $2 ORDER BY due_at",
             static_cast<std::int64_t>(now), static_cast<std::int64_t>(now + lookAhead_))) {
        items.push_back({row["student_id"].as<std::string>(), row["id"].as<std::string>(),
                         row["title"].as<std::string>(), row["due_at"].as<std::int64_t>()});
    }
    return items;
}

}  // namespace cramberry
