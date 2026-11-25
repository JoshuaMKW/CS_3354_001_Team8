#pragma once

#include "data-api/Database.h"
#include "notifications/ReminderScheduler.h"
#include "tasks/Task.h"

namespace cramberry {

class SqlTaskRepository : public ITaskRepository {
public:
    explicit SqlTaskRepository(const data::Database& db) : db_(db) {}

    std::vector<Course> listCourses(const std::string& studentId) const override;
    std::optional<Course> findCourse(const std::string& studentId, const std::string& id) const override;
    void saveCourse(const Course& course) override;
    bool deleteCourse(const std::string& studentId, const std::string& id) override;

    std::vector<Task> listTasks(const std::string& studentId, const TaskFilter& filter) const override;
    std::optional<Task> findTask(const std::string& studentId, const std::string& id) const override;
    void saveTask(const Task& task) override;
    bool deleteTask(const std::string& studentId, const std::string& id) override;

private:
    const data::Database& db_;
};

// Feeds the ReminderScheduler (UC-16) with every student's unfinished tasks that are
// due within the look-ahead window.
class SqlDeadlineSource : public ITaskSource {
public:
    explicit SqlDeadlineSource(const data::Database& db, Timestamp lookAheadMinutes = 14 * 24 * 60)
        : db_(db), lookAhead_(lookAheadMinutes) {}

    std::vector<DeadlineItem> upcomingDeadlines(Timestamp now) const override;

private:
    const data::Database& db_;
    Timestamp lookAhead_;
};

}  // namespace cramberry
