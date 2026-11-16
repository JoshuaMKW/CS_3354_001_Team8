// Demo entry point. In the real project, copy the wiring from here into your own main().
#include <httplib.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "user/AuthService.h"
#include "user/Crypto.h"
#include "user/UserController.h"
#include "user/UserRepository.h"

#ifndef CRAMBERRY_FRONTEND_DIR
#define CRAMBERRY_FRONTEND_DIR "frontend"
#endif

namespace {
std::string env(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value && *value) ? value : fallback;
}
}  // namespace

int main() {
    try {
        const std::string dbPath = env("CRAMBERRY_DB", "cramberry.db");
        const std::string frontendDir = env("CRAMBERRY_FRONTEND", CRAMBERRY_FRONTEND_DIR);
        const int port = std::stoi(env("CRAMBERRY_PORT", "8080"));
        const bool secureCookies = env("CRAMBERRY_SECURE_COOKIES", "0") == "1";  // set to 1 behind HTTPS

        cramberry::SodiumCrypto crypto;
        cramberry::SqliteUserRepository repository(dbPath);
        cramberry::AuthService auth(repository, crypto);
        cramberry::UserController controller(auth, secureCookies);

        httplib::Server server;
        server.set_payload_max_length(16 * 1024);  // login/signup bodies are tiny
        server.set_default_headers({
            {"X-Content-Type-Options", "nosniff"},
            {"X-Frame-Options", "DENY"},
            {"Referrer-Policy", "same-origin"},
            {"Content-Security-Policy", "default-src 'self'"},
        });
        server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
            res.status = 500;  // never leak internals to the client
            res.set_content(R"({"error":"Something went wrong on our side. Try again."})", "application/json");
        });

        controller.registerRoutes(server);
        if (!server.set_mount_point("/", frontendDir)) {
            std::cerr << "Frontend folder not found: " << frontendDir << "\n";
            return 1;
        }
        server.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_redirect("/Login.html");
        });

        std::cout << "Cramberry running at http://localhost:" << port << "\n";
        if (!server.listen("127.0.0.1", port)) {  // localhost only; put HTTPS in front before exposing it
            std::cerr << "Could not listen on port " << port << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "Fatal: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
