#include "user/UserRoutes.h"

#include "common/Json.h"

namespace cramberry {

using drogon::Delete;
using drogon::Get;
using drogon::Patch;
using drogon::Post;

namespace {

Json::Value toJson(const SignedIn& session) {
    Json::Value v;
    v["token"] = session.token;
    v["expiresAt"] = json::formatIsoTimestamp(session.expiresAt);
    v["student"] = cramberry::toJson(session.student);
    return v;
}

std::string text(const Json::Value& body, const char* key) {
    return body[key].isString() ? body[key].asString() : "";
}

// Password hashing is deliberately slow (~0.3 s), so sign-up, sign-in and account
// deletion run on the worker queue instead of blocking Drogon's I/O threads.
AuthGuard::Route offload(trantor::ConcurrentTaskQueue& queue, http::Handler handler) {
    return [&queue, handler = std::move(handler)](const http::HttpRequestPtr& request,
                                                  http::Callback&& callback) {
        queue.runTaskInQueue([request, handler, callback = std::move(callback)] {
            callback(http::safely([&] { return handler(request); }));
        });
    };
}

}  // namespace

Json::Value toJson(const Student& student) {
    Json::Value v;
    v["id"] = student.id;
    v["name"] = student.name;
    v["email"] = student.email;
    v["utcOffsetMinutes"] = student.utcOffsetMinutes;
    v["createdAt"] = json::formatIsoTimestamp(student.createdAt);
    return v;
}

void registerUserRoutes(drogon::HttpAppFramework& app, UserController& users,
                        const AuthGuard& guard, trantor::ConcurrentTaskQueue& slowWork) {
    app.registerHandler(
        "/api/auth/register",
        offload(slowWork,
                [&users](const http::HttpRequestPtr& req) {
                    const auto body = http::body(req);
                    if (!body) return http::badBody();
                    const int offset = (*body)["utcOffsetMinutes"].isInt()
                                           ? (*body)["utcOffsetMinutes"].asInt()
                                           : 0;
                    const auto outcome =
                        users.registerStudent(text(*body, "name"), text(*body, "email"),
                                              text(*body, "password"), offset, json::nowMinutes());
                    return http::respond(outcome, [](const SignedIn& s) { return toJson(s); },
                                         drogon::k201Created);
                }),
        {Post});

    app.registerHandler(
        "/api/auth/login",
        offload(slowWork,
                [&users](const http::HttpRequestPtr& req) {
                    const auto body = http::body(req);
                    if (!body) return http::badBody();
                    const auto outcome = users.login(text(*body, "email"), text(*body, "password"),
                                                     json::nowMinutes());
                    return http::respond(outcome, [](const SignedIn& s) { return toJson(s); });
                }),
        {Post});

    app.registerHandler("/api/auth/logout",
                        guard([&users](const http::HttpRequestPtr& req, const std::string&) {
                            users.logout(http::bearerToken(req).value_or(""));
                            return http::noContent();
                        }),
                        {Post});

    app.registerHandler("/api/users/me",
                        guard([&users](const http::HttpRequestPtr&, const std::string& studentId) {
                            return http::respond(users.profile(studentId),
                                                 [](const Student& s) { return toJson(s); });
                        }),
                        {Get});

    app.registerHandler(
        "/api/users/me",
        guard([&users](const http::HttpRequestPtr& req, const std::string& studentId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            ProfileUpdate update;
            if (body->isMember("name")) {
                if (!(*body)["name"].isString()) return http::error(drogon::k400BadRequest, "name must be text");
                update.name = (*body)["name"].asString();
            }
            if (body->isMember("utcOffsetMinutes")) {
                if (!(*body)["utcOffsetMinutes"].isInt()) {
                    return http::error(drogon::k400BadRequest, "utcOffsetMinutes must be a whole number");
                }
                update.utcOffsetMinutes = (*body)["utcOffsetMinutes"].asInt();
            }
            return http::respond(users.updateProfile(studentId, update),
                                 [](const Student& s) { return toJson(s); });
        }),
        {Patch});

    app.registerHandler(
        "/api/users/me",
        guard(
            [&users](const http::HttpRequestPtr& req, const std::string& studentId) {
                const auto body = http::body(req);
                if (!body) return http::badBody();
                const auto outcome = users.deleteAccount(studentId, text(*body, "password"));
                if (!outcome) return http::error(outcome.error());
                return http::noContent();
            },
            AuthGuard::Speed::Slow),
        {Delete});
}

}  // namespace cramberry
