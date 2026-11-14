#pragma once
// RM: UC-13 task status and UC-14 weekly reporting. Storage is injected so
// unit tests use fakes and a future Supabase adapter can use the same interface.
#include <chrono>
#include <optional>
#include <string>
#include <vector>
namespace cramberry {
using TaskTime = std::chrono::sys_seconds;
struct Task {
    std::string id, studentId, title;
    std::string status = "Not Started";
    TaskTime due{};
    std::optional<TaskTime> completedAt;
};
class ITaskRepository {
public:
    virtual ~ITaskRepository() = default;
    virtual std::optional<Task> find(const std::string& student, const std::string& id) const = 0;
    // Atomic: a failed save must leave the previous record unchanged.
    virtual bool save(const Task& task) = 0;
    virtual std::vector<Task> list(const std::string& student) const = 0;
};
struct TaskResult { bool ok; std::string message; };
struct WeeklyReport {
    bool ok = false;
    std::string message;
    std::vector<Task> completed, overdue;
};
class TaskController {
public:
    explicit TaskController(ITaskRepository& repository) : repository_(repository) {}
    TaskResult updateStatus(const std::string& student, const std::string& id,
                            const std::string& status, TaskTime now);
    // startDate is YYYY-MM-DD, the selected week's start in UTC for this prototype.
    // Completed = completed within [start, start+7 days), as of now.
    // Overdue = still incomplete and due within that week before now.
    WeeklyReport weeklyReport(const std::string& student, const std::string& startDate,
                              TaskTime now) const;
private:
    ITaskRepository& repository_;
};
}
