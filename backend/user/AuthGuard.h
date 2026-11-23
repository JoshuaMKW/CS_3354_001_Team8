#pragma once

#include <trantor/utils/ConcurrentTaskQueue.h>

#include <functional>
#include <string>

#include "common/Http.h"
#include "user/UserController.h"

namespace cramberry {

// Wraps handlers for routes that need a signed-in student (FR-2, NFR-19). The student
// ID handed to the handler comes from the session token, never from the request body
// or URL, so a student can only ever reach their own data.
class AuthGuard {
public:
    using Handler = std::function<http::HttpResponsePtr(const http::HttpRequestPtr&,
                                                        const std::string& studentId)>;
    using HandlerWithId = std::function<http::HttpResponsePtr(
        const http::HttpRequestPtr&, const std::string& studentId, const std::string& id)>;

    using Route = std::function<void(const http::HttpRequestPtr&, http::Callback&&)>;
    using RouteWithId =
        std::function<void(const http::HttpRequestPtr&, http::Callback&&, const std::string&)>;

    // slowWork runs handlers marked Slow (Gemini calls) off Drogon's I/O threads.
    AuthGuard(const UserController& users, trantor::ConcurrentTaskQueue& slowWork)
        : users_(users), slowWork_(slowWork) {}

    enum class Speed { Fast, Slow };

    Route operator()(Handler handler, Speed speed = Speed::Fast) const;
    RouteWithId withId(HandlerWithId handler, Speed speed = Speed::Fast) const;

private:
    void run(const http::HttpRequestPtr& request, http::Callback&& callback, Speed speed,
             std::function<http::HttpResponsePtr(const std::string&)> work) const;

    const UserController& users_;
    trantor::ConcurrentTaskQueue& slowWork_;
};

}  // namespace cramberry
