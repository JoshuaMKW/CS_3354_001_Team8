#include "user/UserController.h"

#include <nlohmann/json.hpp>

#include <optional>

namespace cramberry {
namespace {

using json = nlohmann::json;
constexpr const char* kCookieName = "cramberry_session";

void sendJson(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body.dump(), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& message) {
    sendJson(res, status, json{{"error", message}});
}

json userJson(const User& user) {
    return json{{"id", user.id}, {"username", user.username}, {"email", user.email}};
}

// Requiring a JSON content type blocks plain cross-site <form> posts (they can't send it).
std::optional<json> readJsonBody(const httplib::Request& req, httplib::Response& res) {
    if (req.get_header_value("Content-Type").rfind("application/json", 0) != 0) {
        sendError(res, 415, "Send the request as JSON.");
        return std::nullopt;
    }
    json body = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object()) {
        sendError(res, 400, "The request body must be a JSON object.");
        return std::nullopt;
    }
    return body;
}

std::string stringField(const json& body, const char* key) {
    const auto it = body.find(key);
    return (it != body.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::string cookieValue(const httplib::Request& req, const std::string& name) {
    const std::string header = req.get_header_value("Cookie");  // "a=1; b=2"
    std::size_t pos = 0;
    while (pos < header.size()) {
        std::size_t end = header.find(';', pos);
        if (end == std::string::npos) end = header.size();
        std::string pair = header.substr(pos, end - pos);
        const std::size_t start = pair.find_first_not_of(' ');
        if (start != std::string::npos) pair = pair.substr(start);
        const std::size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == name) return pair.substr(eq + 1);
        pos = end + 1;
    }
    return {};
}

int statusFor(AuthError error) {
    switch (error) {
        case AuthError::None:           return 200;
        case AuthError::InvalidInput:   return 400;
        case AuthError::BadCredentials: return 401;
        case AuthError::Deactivated:    return 403;
        case AuthError::EmailTaken:
        case AuthError::UsernameTaken:  return 409;
        case AuthError::RateLimited:    return 429;
    }
    return 500;
}

}  // namespace

UserController::UserController(AuthService& auth, bool secureCookies)
    : auth_(auth), secureCookies_(secureCookies) {}

void UserController::registerRoutes(httplib::Server& server) {
    server.Post("/api/signup", [this](const httplib::Request& req, httplib::Response& res) { handleSignup(req, res); });
    server.Post("/api/login",  [this](const httplib::Request& req, httplib::Response& res) { handleLogin(req, res); });
    server.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) { handleLogout(req, res); });
    server.Get("/api/me",      [this](const httplib::Request& req, httplib::Response& res) { handleMe(req, res); });
}

std::string UserController::sessionCookie(const std::string& token, int maxAgeSeconds) const {
    // HttpOnly: JavaScript can't read it. SameSite=Strict: never sent on cross-site requests.
    std::string cookie = std::string(kCookieName) + "=" + token +
                         "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAgeSeconds);
    if (secureCookies_) cookie += "; Secure";
    return cookie;
}

void UserController::handleSignup(const httplib::Request& req, httplib::Response& res) {
    const auto body = readJsonBody(req, res);
    if (!body) return;

    const AuthResult result = auth_.signup(stringField(*body, "email"), stringField(*body, "username"),
                                           stringField(*body, "password"));
    if (!result.ok()) return sendError(res, statusFor(result.error), result.message);

    res.set_header("Set-Cookie", sessionCookie(result.sessionToken, auth_.sessionSeconds()));
    sendJson(res, 201, json{{"user", userJson(*result.user)}});
}

void UserController::handleLogin(const httplib::Request& req, httplib::Response& res) {
    const auto body = readJsonBody(req, res);
    if (!body) return;

    // remote_addr is the socket peer. Don't swap in X-Forwarded-For unless a trusted proxy sets it.
    const AuthResult result =
        auth_.login(stringField(*body, "email"), stringField(*body, "password"), req.remote_addr);
    if (!result.ok()) return sendError(res, statusFor(result.error), result.message);

    res.set_header("Set-Cookie", sessionCookie(result.sessionToken, auth_.sessionSeconds()));
    sendJson(res, 200, json{{"user", userJson(*result.user)}});
}

void UserController::handleLogout(const httplib::Request& req, httplib::Response& res) {
    auth_.logout(cookieValue(req, kCookieName));
    res.set_header("Set-Cookie", sessionCookie("", 0));  // expire the cookie
    sendJson(res, 200, json{{"ok", true}});
}

void UserController::handleMe(const httplib::Request& req, httplib::Response& res) {
    const std::optional<User> user = auth_.userForSession(cookieValue(req, kCookieName));
    if (!user) return sendError(res, 401, "Sign in to continue.");
    sendJson(res, 200, json{{"user", userJson(*user)}});
}

}  // namespace cramberry
