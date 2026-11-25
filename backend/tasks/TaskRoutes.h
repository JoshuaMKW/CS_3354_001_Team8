#pragma once

#include <drogon/HttpAppFramework.h>

#include <functional>

#include "tasks/TaskController.h"
#include "user/AuthGuard.h"

namespace cramberry {

// GET    /api/courses                 POST /api/courses        {name, code?, term?}
// PATCH  /api/courses/{id}            DELETE /api/courses/{id}
// GET    /api/tasks?courseId=&from=&to=&status=&overdue=true
// POST   /api/tasks                   {title, dueAt, type?, courseId?, estimatedMinutes?, priority?, notes?, status?}
// GET    /api/tasks/{id}              PATCH /api/tasks/{id}    DELETE /api/tasks/{id}
// PUT    /api/tasks/{id}/status       {status: NOT_STARTED | IN_PROGRESS | COMPLETED}
// GET    /api/reports/weekly?start=YYYY-MM-DD
// onChange runs after any task is created, edited, re-statused or deleted (used to
// re-plan the student's schedule, "Plan Updating" in the SOW).
using TasksChanged = std::function<void(const std::string& studentId)>;
void registerTaskRoutes(drogon::HttpAppFramework& app, TaskController& tasks, const AuthGuard& guard,
                        TasksChanged onChange = {});

Json::Value toJson(const Course& course);
Json::Value toJson(const Task& task, Timestamp now);

}  // namespace cramberry
