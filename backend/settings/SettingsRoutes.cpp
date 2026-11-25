#include "settings/SettingsRoutes.h"

#include "common/Json.h"

namespace cramberry {

void registerSettingsRoutes(drogon::HttpAppFramework& app, SettingsController& controller,
                            const ISettingsRepository& settings, const AuthGuard& guard) {
    app.registerHandler(
        "/api/settings/preferences",
        guard([&settings](const http::HttpRequestPtr&, const std::string& studentId) {
            const auto saved = settings.find(studentId);
            Json::Value v;
            v["saved"] = saved.has_value();
            v["preferences"] = json::toJson(saved.value_or(StudyPreferences{}));
            return http::json(v);
        }),
        {drogon::Get});

    app.registerHandler(
        "/api/settings/preferences",
        guard([&controller](const http::HttpRequestPtr& req, const std::string& studentId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            std::string error;
            const auto prefs = json::preferencesFromJson(*body, error);
            if (!prefs) return http::error(drogon::k400BadRequest, error);

            const auto result = controller.saveStudyPreferences(studentId, *prefs);
            switch (result.status) {
                case SaveResult::Status::Saved: {
                    Json::Value v;
                    v["message"] = result.message;
                    v["preferences"] = json::toJson(*prefs);
                    return http::json(v);
                }
                case SaveResult::Status::InvalidInput:
                    return http::error(drogon::k400BadRequest, result.message);
                case SaveResult::Status::Conflict: {
                    Json::Value v;
                    v["error"] = result.message;
                    v["conflicts"] = Json::arrayValue;
                    for (const auto& block : result.conflicts) v["conflicts"].append(json::toJson(block));
                    return http::json(v, drogon::k409Conflict);
                }
                case SaveResult::Status::Error:
                    break;
            }
            return http::error(drogon::k500InternalServerError, result.message);
        }),
        {drogon::Put});
}

}  // namespace cramberry
