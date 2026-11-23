#include "user/AuthGuard.h"

#include "common/Json.h"

namespace cramberry {

void AuthGuard::run(const http::HttpRequestPtr& request, http::Callback&& callback, Speed speed,
                    std::function<http::HttpResponsePtr(const std::string&)> work) const {
    std::optional<std::string> studentId;
    const auto authResponse = http::safely([&]() -> http::HttpResponsePtr {
        const auto token = http::bearerToken(request);
        if (token) studentId = users_.authenticate(*token, json::nowMinutes());
        if (studentId) return nullptr;
        return http::error(drogon::k401Unauthorized, "Please sign in again");
    });
    if (authResponse) {
        callback(authResponse);
        return;
    }
    if (speed == Speed::Fast) {
        callback(http::safely([&] { return work(*studentId); }));
        return;
    }
    slowWork_.runTaskInQueue(
        [callback = std::move(callback), work = std::move(work), id = *studentId]() {
            callback(http::safely([&] { return work(id); }));
        });
}

AuthGuard::Route AuthGuard::operator()(Handler handler, Speed speed) const {
    return [this, handler = std::move(handler), speed](const http::HttpRequestPtr& request,
                                                       http::Callback&& callback) {
        run(request, std::move(callback), speed,
            [handler, request](const std::string& studentId) { return handler(request, studentId); });
    };
}

AuthGuard::RouteWithId AuthGuard::withId(HandlerWithId handler, Speed speed) const {
    return [this, handler = std::move(handler), speed](const http::HttpRequestPtr& request,
                                                       http::Callback&& callback,
                                                       const std::string& id) {
        run(request, std::move(callback), speed,
            [handler, request, id](const std::string& studentId) {
                return handler(request, studentId, id);
            });
    };
}

}  // namespace cramberry
