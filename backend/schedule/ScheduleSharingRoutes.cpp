#include "schedule/ScheduleSharingRoutes.h"

#include "common/Http.h"
#include "common/Json.h"

namespace cramberry {

namespace {

Json::Value shareJson(const ScheduleShare& share, bool ownerView) {
    Json::Value value;
    value["id"] = share.id;
    value["createdAt"] = json::formatIsoTimestamp(share.createdAt);
    if (ownerView) {
        value["recipientId"] = share.recipientId;
        value["recipientEmail"] = share.recipientEmail;
    } else {
        value["ownerId"] = share.ownerId;
        value["ownerName"] = share.ownerName;
        value["viewEndpoint"] = "/api/schedule/shared-with-me/" + share.id;
    }
    return value;
}

Json::Value sharesJson(const std::vector<ScheduleShare>& shares, bool ownerView) {
    Json::Value result(Json::arrayValue);
    for (const auto& share : shares) result.append(shareJson(share, ownerView));
    return result;
}

}  // namespace

void registerScheduleSharingRoutes(drogon::HttpAppFramework& app,
                                   ScheduleSharingController& sharing,
                                   const AuthGuard& guard) {
    app.registerHandler(
        "/api/schedule/shares",
        guard([&sharing](const http::HttpRequestPtr& req, const std::string& ownerId) {
            const auto body = http::body(req);
            if (!body) return http::badBody();
            if (!(*body)["email"].isString()) {
                return http::error(drogon::k400BadRequest, "email is required");
            }
            const auto result = sharing.shareWith(ownerId, (*body)["email"].asString(),
                                                  json::nowMinutes());
            if (!result) return http::error(result.error());
            Json::Value response = shareJson(result.value(), true);
            response["shareId"] = result.value().id;
            response["viewEndpoint"] = "/api/schedule/shared-with-me/" + result.value().id;
            return http::json(response, drogon::k201Created);
        }),
        {drogon::Post});

    app.registerHandler(
        "/api/schedule/shares",
        guard([&sharing](const http::HttpRequestPtr&, const std::string& ownerId) {
            Json::Value response;
            response["shares"] = sharesJson(sharing.listOwnedShares(ownerId), true);
            return http::json(response);
        }),
        {drogon::Get});

    app.registerHandler(
        "/api/schedule/shares/{shareId}",
        guard([&sharing](const http::HttpRequestPtr& req, const std::string& ownerId) {
            const auto result = sharing.revoke(ownerId, req->getParameter("shareId"));
            if (!result) return http::error(result.error());
            return http::noContent();
        }),
        {drogon::Delete});

    app.registerHandler(
        "/api/schedule/shared-with-me",
        guard([&sharing](const http::HttpRequestPtr&, const std::string& recipientId) {
            Json::Value response;
            response["shares"] = sharesJson(sharing.listReceivedShares(recipientId), false);
            return http::json(response);
        }),
        {drogon::Get});

    app.registerHandler(
        "/api/schedule/shared-with-me/{shareId}",
        guard([&sharing](const http::HttpRequestPtr& req, const std::string& recipientId) {
            const auto result = sharing.viewShared(recipientId, req->getParameter("shareId"));
            if (!result) return http::error(result.error());
            Json::Value response = shareJson(result.value().share, false);
            response["blocks"] = Json::arrayValue;
            for (const auto& block : result.value().blocks) {
                response["blocks"].append(json::toJson(block));
            }
            return http::json(response);
        }),
        {drogon::Get});
}

}  // namespace cramberry
