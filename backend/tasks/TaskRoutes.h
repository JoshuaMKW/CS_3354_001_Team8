#pragma once

#include <drogon/HttpAppFramework.h>

#include <cstdint>
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
// It returns the ticket of the re-plan it queued, or 0 for none; a non-zero ticket is sent back
// in the X-Replan-Ticket header so the client can ask GET /api/agent/replans/{ticket} how it went.
using TasksChanged = std::function<std::uint64_t(const std::string& studentId)>;
void registerTaskRoutes(drogon::HttpAppFramework& app, TaskController& tasks, const AuthGuard& guard,
                        TasksChanged onChange = {});

Json::Value toJson(const Course& course);
Json::Value toJson(const Task& task, Timestamp now);

}  // namespace cramberry
