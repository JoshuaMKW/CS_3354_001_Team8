#include "agent/AgentRoutes.h"

#include "common/Json.h"

namespace cramberry {

void registerAgentRoutes(drogon::HttpAppFramework& app, AgentController& agent, const AuthGuard& guard) {
    app.registerHandler(
        "/api/agent/generate",
        guard(
            [&agent](const http::HttpRequestPtr& req, const std::string& studentId) {
                std::optional<Day> onlyDay;
                if (const auto body = http::body(req); body && body->isMember("day")) {
                    const auto& day = (*body)["day"];
                    onlyDay = day.isString() ? json::parseDay(day.asString()) : std::nullopt;
                    if (!onlyDay) return http::error(drogon::k400BadRequest, "day must be Mon, Tue, ... or Sun");
                }
                return http::respond(agent.generate(studentId, onlyDay, json::nowMinutes()),
                                     [](const GeneratedPlan& plan) {
                                         Json::Value v;
                                         v["engine"] = plan.engine;
                                         v["schedule"] = Json::arrayValue;
                                         for (const auto& b : plan.blocks) v["schedule"].append(json::toJson(b));
                                         v["warnings"] = Json::arrayValue;
                                         for (const auto& w : plan.warnings) v["warnings"].append(w);
                                         return v;
                                     });
            },
            AuthGuard::Speed::Slow),
        {drogon::Post});

    app.registerHandler(
        "/api/agent/chat",
        guard(
            [&agent](const http::HttpRequestPtr& req, const std::string& studentId) {
                const auto body = http::body(req);
                if (!body) return http::badBody();
                const std::string message =
                    (*body)["message"].isString() ? (*body)["message"].asString() : "";
                return http::respond(agent.chat(studentId, message, json::nowMinutes()),
                                     [](const std::string& reply) {
                                         Json::Value v;
                                         v["reply"] = reply;
                                         return v;
                                     });
            },
            AuthGuard::Speed::Slow),
        {drogon::Post});

    app.registerHandler("/api/agent/runs",
                        guard([&agent](const http::HttpRequestPtr&, const std::string& studentId) {
                            Json::Value list(Json::arrayValue);
                            for (const auto& run : agent.history(studentId)) {
                                Json::Value v;
                                v["id"] = run.id;
                                v["createdAt"] = json::formatIsoTimestamp(run.createdAt);
                                v["engine"] = run.engine;
                                v["ok"] = run.ok;
                                v["blockCount"] = run.blockCount;
                                v["details"] = Json::arrayValue;
                                for (const auto& d : run.details) v["details"].append(d);
                                list.append(v);
                            }
                            return http::json(list);
                        }),
                        {drogon::Get});
}

}  // namespace cramberry
