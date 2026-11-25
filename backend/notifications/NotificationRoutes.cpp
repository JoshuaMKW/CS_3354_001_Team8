#include "notifications/NotificationRoutes.h"

#include "common/Json.h"

namespace cramberry {

void registerNotificationRoutes(drogon::HttpAppFramework& app, NotificationController& controller,
                                const INotificationRepository& settings, SqlInbox& inbox,
                                const AuthGuard& guard) {
    app.registerHandler(
        "/api/settings/notifications",
        guard([&settings](const http::HttpRequestPtr&, const std::string& studentId) {
            return http::json(json::toJson(settings.find(studentId).value_or(NotificationSettings{})));
        }),
        {drogon::Get});

    app.registerHandler(
        "/api/settings/notifications",
        guard([&controller](const http::HttpRequestPtr& req, const std::string& studentId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            std::string error;
            const auto parsed = json::notificationSettingsFromJson(*body, error);
            if (!parsed) return http::error(drogon::k400BadRequest, error);
            if (auto invalid = NotificationController::validatePreferences(*parsed)) {
                return http::error(drogon::k400BadRequest, *invalid);
            }
            const auto result = controller.updateSettings(studentId, *parsed);
            if (!result.ok) return http::error(drogon::k500InternalServerError, result.message);
            Json::Value v;
            v["message"] = result.message;
            v["settings"] = json::toJson(*parsed);
            return http::json(v);
        }),
        {drogon::Put});

    app.registerHandler(
        "/api/notifications",
        guard([&inbox](const http::HttpRequestPtr& req, const std::string& studentId) {
            Json::Value list(Json::arrayValue);
            for (const auto& item : inbox.list(studentId, req->getParameter("unread") == "true", 100)) {
                Json::Value v;
                v["id"] = item.id;
                v["channel"] = json::channelName(item.channel);
                v["subject"] = item.subject;
                v["body"] = item.body;
                v["sentAt"] = json::formatIsoTimestamp(item.sentAt);
                v["read"] = item.read;
                list.append(v);
            }
            return http::json(list);
        }),
        {drogon::Get});

    // Registered before /{id}/read so "read-all" isn't taken as an id.
    app.registerHandler("/api/notifications/read-all",
                        guard([&inbox](const http::HttpRequestPtr&, const std::string& studentId) {
                            inbox.markAllRead(studentId);
                            return http::noContent();
                        }),
                        {drogon::Post});

    app.registerHandler("/api/notifications/{id}/read",
                        guard.withId([&inbox](const http::HttpRequestPtr&, const std::string& studentId,
                                              const std::string& id) {
                            if (!inbox.markRead(studentId, id)) {
                                return http::error(drogon::k404NotFound, "Notification not found");
                            }
                            return http::noContent();
                        }),
                        {drogon::Post});
}

}  // namespace cramberry
