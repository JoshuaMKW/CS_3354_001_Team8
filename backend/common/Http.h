#pragma once

// Helpers shared by every module's Routes file: JSON responses, error mapping and
// exception safety. Errors always look like {"error": "...", "details": [...]}.

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/Outcome.h"

namespace cramberry::http {

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using Callback = std::function<void(const HttpResponsePtr&)>;
using Handler = std::function<HttpResponsePtr(const HttpRequestPtr&)>;

HttpResponsePtr json(const Json::Value& body, drogon::HttpStatusCode status = drogon::k200OK);
HttpResponsePtr noContent();
HttpResponsePtr error(drogon::HttpStatusCode status, const std::string& message,
                      const std::vector<std::string>& details = {});
HttpResponsePtr error(const Error& error);
HttpResponsePtr badBody();

// The request body as JSON, or nullptr if it is missing or not valid JSON.
std::shared_ptr<const Json::Value> body(const HttpRequestPtr& request);

// "Authorization: Bearer <token>" -> token.
std::optional<std::string> bearerToken(const HttpRequestPtr& request);

// Runs a handler and turns any exception into a 500 that leaks no internals.
HttpResponsePtr safely(const std::function<HttpResponsePtr()>& handler);

// Wraps a handler for a route that needs no sign-in.
std::function<void(const HttpRequestPtr&, Callback&&)> open(Handler handler);

template <typename T, typename ToJson>
HttpResponsePtr respond(const Outcome<T>& outcome, ToJson&& toJson,
                        drogon::HttpStatusCode status = drogon::k200OK) {
    if (!outcome.ok()) return error(outcome.error());
    return json(toJson(outcome.value()), status);
}

}  // namespace cramberry::http
