#include "common/Http.h"

#include <drogon/orm/Exception.h>
#include <trantor/utils/Logger.h>

#include <exception>

#include "common/Json.h"

namespace cramberry::http {

namespace {

drogon::HttpStatusCode statusFor(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidInput: return drogon::k400BadRequest;
        case ErrorCode::Unauthorized: return drogon::k401Unauthorized;
        case ErrorCode::NotFound: return drogon::k404NotFound;
        case ErrorCode::Conflict: return drogon::k409Conflict;
        case ErrorCode::Unprocessable: return drogon::k422UnprocessableEntity;
        case ErrorCode::Unavailable: return drogon::k503ServiceUnavailable;
    }
    return drogon::k500InternalServerError;
}

}  // namespace

HttpResponsePtr json(const Json::Value& body, drogon::HttpStatusCode status) {
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    return response;
}

HttpResponsePtr noContent() {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    return response;
}

HttpResponsePtr error(drogon::HttpStatusCode status, const std::string& message,
                      const std::vector<std::string>& details) {
    Json::Value body;
    body["error"] = message;
    if (!details.empty()) {
        Json::Value list(Json::arrayValue);
        for (const auto& d : details) list.append(d);
        body["details"] = list;
    }
    return json(body, status);
}

HttpResponsePtr error(const Error& e) { return error(statusFor(e.code), e.message, e.details); }

HttpResponsePtr badBody() {
    return error(drogon::k400BadRequest, "Request body must be a JSON object");
}

std::shared_ptr<const Json::Value> body(const HttpRequestPtr& request) {
    if (auto parsed = request->getJsonObject(); parsed && parsed->isObject()) return parsed;
    // Accept JSON sent without a Content-Type header too.
    if (auto parsed = json::parse(request->body()); parsed && parsed->isObject()) {
        return std::make_shared<const Json::Value>(std::move(*parsed));
    }
    return nullptr;
}

std::optional<std::string> bearerToken(const HttpRequestPtr& request) {
    const std::string& header = request->getHeader("authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (header.size() <= prefix.size() || header.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    return header.substr(prefix.size());
}

HttpResponsePtr safely(const std::function<HttpResponsePtr()>& handler) {
    try {
        return handler();
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
    } catch (const std::exception& e) {
        LOG_ERROR << "Unhandled error: " << e.what();
    } catch (...) {
        LOG_ERROR << "Unhandled non-standard exception";
    }
    return error(drogon::k500InternalServerError, "Something went wrong on our side. Please try again.");
}

std::function<void(const HttpRequestPtr&, Callback&&)> open(Handler handler) {
    return [handler = std::move(handler)](const HttpRequestPtr& request, Callback&& callback) {
        callback(safely([&] { return handler(request); }));
    };
}

}  // namespace cramberry::http
