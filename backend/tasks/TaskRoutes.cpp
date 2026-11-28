#include "tasks/TaskRoutes.h"

#include "common/Json.h"

namespace cramberry {

using drogon::Delete;
using drogon::Get;
using drogon::Patch;
using drogon::Post;
using drogon::Put;

namespace {

bool readCourse(const Json::Value& body, CourseInput& input, std::string& error) {
    for (const char* key : {"name", "code", "term"}) {
        if (body.isMember(key) && !body[key].isString()) {
            error = std::string(key) + " must be text";
            return false;
        }
    }
    if (body.isMember("name")) input.name = body["name"].asString();
    if (body.isMember("code")) input.code = body["code"].asString();
    if (body.isMember("term")) input.term = body["term"].asString();
    return true;
}

// Copies the fields present in body onto task; used by both create and PATCH.
bool applyTaskJson(const Json::Value& body, Task& task, std::string& error) {
    auto text = [&](const char* key, std::string& out) {
        if (!body.isMember(key)) return true;
        if (body[key].isNull()) {
            out.clear();
            return true;
        }
        if (!body[key].isString()) {
            error = std::string(key) + " must be text";
            return false;
        }
        out = body[key].asString();
        return true;
    };
    auto integer = [&](const char* key, int& out) {
        if (!body.isMember(key)) return true;
        if (!body[key].isInt()) {
            error = std::string(key) + " must be a whole number";
            return false;
        }
        out = body[key].asInt();
        return true;
    };
    if (!text("title", task.title) || !text("notes", task.notes) ||
        !text("courseId", task.courseId) || !integer("estimatedMinutes", task.estimatedMinutes) ||
        !integer("priority", task.priority)) {
        return false;
    }
    if (body.isMember("type")) {
        const auto type = body["type"].isString() ? parseTaskType(body["type"].asString()) : std::nullopt;
        if (!type) {
            error = "type must be assignment, exam, study or other";
            return false;
        }
        task.type = *type;
    }
    if (body.isMember("status")) {
        const auto status =
            body["status"].isString() ? parseTaskStatus(body["status"].asString()) : std::nullopt;
        if (!status) {
            error = "status must be NOT_STARTED, IN_PROGRESS or COMPLETED";
            return false;
        }
        task.status = *status;
    }
    if (body.isMember("dueAt")) {
        const auto due =
            body["dueAt"].isString() ? json::parseIsoTimestamp(body["dueAt"].asString()) : std::nullopt;
        if (!due) {
            error = "dueAt must be a UTC date/time like 2026-12-01T23:59:00Z";
            return false;
        }
        task.dueAt = *due;
    }
    return true;
}

}  // namespace

Json::Value toJson(const Course& course) {
    Json::Value v;
    v["id"] = course.id;
    v["name"] = course.name;
    v["code"] = course.code;
    v["term"] = course.term;
    return v;
}

Json::Value toJson(const Task& task, Timestamp now) {
    Json::Value v;
    v["id"] = task.id;
    v["courseId"] = task.courseId.empty() ? Json::Value() : Json::Value(task.courseId);
    v["title"] = task.title;
    v["notes"] = task.notes;
    v["type"] = toString(task.type);
    v["dueAt"] = json::formatIsoTimestamp(task.dueAt);
    v["estimatedMinutes"] = task.estimatedMinutes;
    v["priority"] = task.priority;
    v["status"] = toString(task.status);
    v["completedAt"] = task.completedAt ? Json::Value(json::formatIsoTimestamp(*task.completedAt)) : Json::Value();
    v["createdAt"] = json::formatIsoTimestamp(task.createdAt);
    v["overdue"] = task.isOverdue(now);
    return v;
}

void registerTaskRoutes(drogon::HttpAppFramework& app, TaskController& tasks, const AuthGuard& guard,
                        TasksChanged onChange) {
    auto taskJson = [](const Task& t) { return toJson(t, json::nowMinutes()); };
    // Tells onChange about successful writes that change the plan, and adds the re-plan ticket
    // it returns.
    auto notify = [onChange](const std::string& studentId, http::HttpResponsePtr response,
                             bool changesPlan = true) {
        if (changesPlan && onChange && static_cast<int>(response->statusCode()) < 300) {
            if (const auto ticket = onChange(studentId)) {
                response->addHeader("X-Replan-Ticket", std::to_string(ticket));
            }
        }
        return response;
    };
    auto courseJson = [](const Course& c) { return toJson(c); };

    // ---- Courses ----
    app.registerHandler("/api/courses",
                        guard([&tasks](const http::HttpRequestPtr&, const std::string& studentId) {
                            Json::Value list(Json::arrayValue);
                            for (const auto& c : tasks.listCourses(studentId)) list.append(toJson(c));
                            return http::json(list);
                        }),
                        {Get});

    app.registerHandler(
        "/api/courses",
        guard([&tasks, courseJson](const http::HttpRequestPtr& req, const std::string& studentId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            CourseInput input;
            std::string error;
            if (!readCourse(*body, input, error)) return http::error(drogon::k400BadRequest, error);
            return http::respond(tasks.createCourse(studentId, input), courseJson, drogon::k201Created);
        }),
        {Post});

    app.registerHandler(
        "/api/courses/{id}",
        guard.withId([&tasks, courseJson](const http::HttpRequestPtr& req, const std::string& studentId,
                                          const std::string& id) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            const auto all = tasks.listCourses(studentId);
            CourseInput input;
            for (const auto& c : all) {
                if (c.id == id) input = {c.name, c.code, c.term};
            }
            std::string error;
            if (!readCourse(*body, input, error)) return http::error(drogon::k400BadRequest, error);
            return http::respond(tasks.updateCourse(studentId, id, input), courseJson);
        }),
        {Patch});

    app.registerHandler(
        "/api/courses/{id}",
        guard.withId([&tasks](const http::HttpRequestPtr&, const std::string& studentId,
                              const std::string& id) {
            const auto outcome = tasks.deleteCourse(studentId, id);
            return outcome ? http::noContent() : http::error(outcome.error());
        }),
        {Delete});

    // ---- Tasks ----
    app.registerHandler(
        "/api/tasks",
        guard([&tasks, taskJson](const http::HttpRequestPtr& req, const std::string& studentId) {
            TaskFilter filter;
            if (const auto& course = req->getParameter("courseId"); !course.empty()) filter.courseId = course;
            if (const auto& from = req->getParameter("from"); !from.empty()) {
                filter.dueFrom = json::parseIsoDateStart(from);
                if (!filter.dueFrom) return http::error(drogon::k400BadRequest, "from must be a date like 2026-12-01");
            }
            if (const auto& to = req->getParameter("to"); !to.empty()) {
                filter.dueTo = json::parseIsoDateStart(to);
                if (!filter.dueTo) return http::error(drogon::k400BadRequest, "to must be a date like 2026-12-08");
            }
            if (const auto& status = req->getParameter("status"); !status.empty()) {
                filter.status = parseTaskStatus(status);
                if (!filter.status) {
                    return http::error(drogon::k400BadRequest,
                                       "status must be NOT_STARTED, IN_PROGRESS or COMPLETED");
                }
            }
            const bool overdueOnly = req->getParameter("overdue") == "true";
            Json::Value list(Json::arrayValue);
            for (const auto& t : tasks.listTasks(studentId, filter, overdueOnly, json::nowMinutes())) {
                list.append(taskJson(t));
            }
            return http::json(list);
        }),
        {Get});

    app.registerHandler(
        "/api/tasks",
        guard([&tasks, taskJson, notify](const http::HttpRequestPtr& req, const std::string& studentId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            Task draft;
            std::string error;
            if (!applyTaskJson(*body, draft, error)) return http::error(drogon::k400BadRequest, error);
            return notify(studentId, http::respond(tasks.createTask(studentId, draft, json::nowMinutes()),
                                                   taskJson, drogon::k201Created));
        }),
        {Post});

    app.registerHandler(
        "/api/tasks/{id}",
        guard.withId([&tasks, taskJson](const http::HttpRequestPtr&, const std::string& studentId,
                                         const std::string& id) {
            return http::respond(tasks.getTask(studentId, id), taskJson);
        }),
        {Get});

    app.registerHandler(
        "/api/tasks/{id}",
        guard.withId([&tasks, taskJson, notify](const http::HttpRequestPtr& req, const std::string& studentId,
                                                 const std::string& id) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            auto existing = tasks.getTask(studentId, id);
            if (!existing) return http::error(existing.error());
            Task edited = existing.value();
            std::string error;
            if (!applyTaskJson(*body, edited, error)) return http::error(drogon::k400BadRequest, error);
            const auto updated = tasks.updateTask(studentId, edited, json::nowMinutes());
            const bool changesPlan = updated && updated.value().changesPlanFrom(existing.value());
            return notify(studentId, http::respond(updated, taskJson), changesPlan);
        }),
        {Patch});

    app.registerHandler(
        "/api/tasks/{id}",
        guard.withId([&tasks, notify](const http::HttpRequestPtr&, const std::string& studentId,
                                      const std::string& id) {
            const auto outcome = tasks.deleteTask(studentId, id);
            return outcome ? notify(studentId, http::noContent()) : http::error(outcome.error());
        }),
        {Delete});

    app.registerHandler(
        "/api/tasks/{id}/status",
        guard.withId([&tasks, taskJson, notify](const http::HttpRequestPtr& req, const std::string& studentId,
                                                 const std::string& id) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            const auto status =
                (*body)["status"].isString() ? parseTaskStatus((*body)["status"].asString()) : std::nullopt;
            if (!status) {
                return http::error(drogon::k400BadRequest,
                                   "status must be NOT_STARTED, IN_PROGRESS or COMPLETED");
            }
            const auto before = tasks.getTask(studentId, id);
            if (!before) return http::error(before.error());
            const auto updated = tasks.setStatus(studentId, id, *status, json::nowMinutes());
            const bool changesPlan = updated && updated.value().changesPlanFrom(before.value());
            return notify(studentId, http::respond(updated, taskJson), changesPlan);
        }),
        {Put});

    // ---- UC-14 weekly progress report ----
    app.registerHandler(
        "/api/reports/weekly",
        guard([&tasks, taskJson](const http::HttpRequestPtr& req, const std::string& studentId) {
            const Timestamp now = json::nowMinutes();
            Timestamp start = json::startOfWeek(now);
            if (const auto& param = req->getParameter("start"); !param.empty()) {
                const auto parsed = json::parseIsoDateStart(param);
                if (!parsed) return http::error(drogon::k400BadRequest, "start must be a date like 2026-11-23");
                start = *parsed;
            }
            return http::respond(tasks.weeklyReport(studentId, start, now), [&](const WeeklyReport& r) {
                Json::Value v;
                v["weekStart"] = json::formatIsoTimestamp(r.weekStart);
                v["weekEnd"] = json::formatIsoTimestamp(r.weekEnd);
                for (const auto* key : {"completed", "overdue", "remaining"}) v[key] = Json::arrayValue;
                for (const auto& t : r.completed) v["completed"].append(taskJson(t));
                for (const auto& t : r.overdue) v["overdue"].append(taskJson(t));
                for (const auto& t : r.remaining) v["remaining"].append(taskJson(t));
                v["averageCompletionMinutes"] =
                    r.averageCompletionMinutes ? Json::Value(*r.averageCompletionMinutes) : Json::Value();
                return v;
            });
        }),
        {Get});
}

}  // namespace cramberry
