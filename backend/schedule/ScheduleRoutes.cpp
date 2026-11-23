#include "schedule/ScheduleRoutes.h"

#include "common/Json.h"
#include "tasks/TaskRoutes.h"
#include "user/UserRoutes.h"

namespace cramberry {

namespace {

// A weekly block plus the real UTC times it falls on this week.
Json::Value blockJson(const StudyBlock& block, const PlanningWeek& week) {
    Json::Value v = json::toJson(block);
    v["startsAt"] = json::formatIsoTimestamp(week.toUtc(block.time.day, block.time.start));
    v["endsAt"] = json::formatIsoTimestamp(week.toUtc(block.time.day, block.time.end));
    return v;
}

Json::Value blocksJson(const std::vector<StudyBlock>& blocks, const PlanningWeek& week) {
    Json::Value list(Json::arrayValue);
    for (const auto& b : blocks) list.append(blockJson(b, week));
    return list;
}

}  // namespace

void registerScheduleRoutes(drogon::HttpAppFramework& app, ScheduleController& schedule,
                            const AuthGuard& guard) {
    app.registerHandler(
        "/api/schedule",
        guard([&schedule](const http::HttpRequestPtr&, const std::string& studentId) {
            const auto week = schedule.week(studentId, json::nowMinutes());
            if (!week) return http::error(week.error());
            Json::Value v;
            v["weekStart"] = json::formatIsoTimestamp(week.value().startUtc);
            v["utcOffsetMinutes"] = week.value().utcOffsetMinutes;
            v["blocks"] = blocksJson(schedule.current(studentId), week.value());
            return http::json(v);
        }),
        {drogon::Get});

    app.registerHandler(
        "/api/schedule/blocks",
        guard([&schedule](const http::HttpRequestPtr& req, const std::string& studentId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            BlockMove move;
            std::string error;
            const auto from = json::timeRangeFromJson((*body)["from"], error);
            if (!from) return http::error(drogon::k400BadRequest, "from: " + error);
            const auto to = json::timeRangeFromJson((*body)["to"], error);
            if (!to) return http::error(drogon::k400BadRequest, "to: " + error);
            if (!(*body)["assignmentId"].isString()) {
                return http::error(drogon::k400BadRequest, "assignmentId is required");
            }
            move.assignmentId = (*body)["assignmentId"].asString();
            move.from = *from;
            move.to = *to;
            if ((*body)["title"].isString()) move.title = (*body)["title"].asString();

            const Timestamp now = json::nowMinutes();
            const auto moved = schedule.moveBlock(studentId, move, now);
            if (!moved) return http::error(moved.error());
            const auto week = schedule.week(studentId, now);
            if (!week) return http::error(week.error());
            Json::Value v;
            v["blocks"] = blocksJson(moved.value(), week.value());
            return http::json(v);
        }),
        {drogon::Patch});

    app.registerHandler("/api/schedule",
                        guard([&schedule](const http::HttpRequestPtr&, const std::string& studentId) {
                            schedule.clear(studentId);
                            return http::noContent();
                        }),
                        {drogon::Delete});

    app.registerHandler(
        "/api/schedule/export.ics",
        guard([&schedule](const http::HttpRequestPtr&, const std::string& studentId) {
            const auto ics = schedule.exportIcs(studentId, json::nowMinutes());
            if (!ics) return http::error(ics.error());
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setBody(ics.value());
            response->setContentTypeString("text/calendar; charset=utf-8");
            response->addHeader("Content-Disposition", "attachment; filename=\"cramberry-schedule.ics\"");
            return response;
        }),
        {drogon::Get});

    app.registerHandler(
        "/api/dashboard",
        guard([&schedule](const http::HttpRequestPtr&, const std::string& studentId) {
            const Timestamp now = json::nowMinutes();
            return http::respond(schedule.dashboard(studentId, now), [now](const Dashboard& d) {
                Json::Value v;
                v["student"] = toJson(d.student);
                v["today"] = dayName(d.today);
                v["hasPreferences"] = d.hasPreferences;
                v["todaysBlocks"] = blocksJson(d.todaysBlocks, d.week);
                v["schedule"] = blocksJson(d.schedule, d.week);
                v["upcoming"] = Json::arrayValue;
                for (const auto& t : d.upcoming) v["upcoming"].append(toJson(t, now));
                v["overdue"] = Json::arrayValue;
                for (const auto& t : d.overdue) v["overdue"].append(toJson(t, now));
                v["counts"]["notStarted"] = d.notStarted;
                v["counts"]["inProgress"] = d.inProgress;
                v["counts"]["completedThisWeek"] = d.completedThisWeek;
                return v;
            });
        }),
        {drogon::Get});
}

}  // namespace cramberry
