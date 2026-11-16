#pragma once
// HTTP layer: turns requests into AuthService calls and results into JSON + cookies.
#include <httplib.h>

#include <string>

#include "user/AuthService.h"

namespace cramberry {

class UserController {
public:
    // secureCookies = true when served over HTTPS (adds the Secure flag).
    UserController(AuthService& auth, bool secureCookies);

    // POST /api/signup   {email, username, password}
    // POST /api/login    {email, password}
    // POST /api/logout
    // GET  /api/me
    void registerRoutes(httplib::Server& server);

private:
    void handleSignup(const httplib::Request& req, httplib::Response& res);
    void handleLogin(const httplib::Request& req, httplib::Response& res);
    void handleLogout(const httplib::Request& req, httplib::Response& res);
    void handleMe(const httplib::Request& req, httplib::Response& res);

    std::string sessionCookie(const std::string& token, int maxAgeSeconds) const;

    AuthService& auth_;
    bool secureCookies_;
};

}  // namespace cramberry
