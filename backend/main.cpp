// Cramberry REST API server (Drogon).
//
// Configuration comes from environment variables (see backend/README.md):
//   CRAMBERRY_HOST, CRAMBERRY_PORT, CRAMBERRY_THREADS
//   CRAMBERRY_DATABASE_URL      Supabase/PostgreSQL connection string; SQLite when unset
//   CRAMBERRY_SQLITE_PATH       SQLite file (default cramberry.db)
//   OPENROUTER_API_KEY, OPENROUTER_MODEL   any OpenRouter model, default Claude Haiku 5.5 (used if set)
//   GEMINI_API_KEY, GEMINI_MODEL           Gemini through Google's API
//   CRAMBERRY_CORS_ORIGIN       e.g. http://localhost:3000 for the Next.js frontend
//   CRAMBERRY_AUTO_REPLAN       1 (default) re-plans after task or preference changes

#include <drogon/drogon.h>
#include <trantor/net/EventLoopThread.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include <cstdlib>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "agent/AgentController.h"
#include "agent/AgentRepository.h"
#include "agent/AgentRoutes.h"
#include "agent/GeminiClient.h"
#include "common/Http.h"
#include "common/Json.h"
#include "data-api/Database.h"
#include "notifications/NotificationController.h"
#include "notifications/NotificationRepository.h"
#include "notifications/NotificationRoutes.h"
#include "notifications/ReminderScheduler.h"
#include "schedule/ScheduleController.h"
#include "schedule/ScheduleRepository.h"
#include "schedule/ScheduleRoutes.h"
#include "settings/SettingsController.h"
#include "settings/SettingsRoutes.h"
#include "settings/SqlSettingsRepository.h"
#include "tasks/TaskController.h"
#include "tasks/TaskRepository.h"
#include "tasks/TaskRoutes.h"
#include "user/AuthGuard.h"
#include "user/UserController.h"
#include "user/UserRepository.h"
#include "user/UserRoutes.h"

namespace {

std::string env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return (value && *value) ? value : fallback;
}

int envInt(const char* name, int fallback) {
    try {
        return std::stoi(env(name, std::to_string(fallback)));
    } catch (...) {
        return fallback;
    }
}

void enableCors(const std::string& origin) {
    if (origin.empty()) return;
    auto addHeaders = [origin](const drogon::HttpResponsePtr& response) {
        response->addHeader("Access-Control-Allow-Origin", origin);
        response->addHeader("Vary", "Origin");
        response->addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        response->addHeader("Access-Control-Max-Age", "600");
    };
    drogon::app().registerPreRoutingAdvice(
        [addHeaders](const drogon::HttpRequestPtr& request, drogon::AdviceCallback&& stop,
                     drogon::AdviceChainCallback&& next) {
            if (request->method() != drogon::Options) {
                next();
                return;
            }
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k204NoContent);
            addHeaders(response);
            stop(response);
        });
    drogon::app().registerPostHandlingAdvice(
        [addHeaders](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& response) {
            addHeaders(response);
        });
}

}  // namespace

int main() {
    using namespace cramberry;
#if defined(_MSC_VER) && defined(_DEBUG)
    // Debug builds on Windows: print failed asserts/abort() to the console and exit,
    // instead of opening a modal Abort/Retry/Ignore dialog per failure.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    try {
        trantor::Logger::setLogLevel(trantor::Logger::kInfo);

        // ---- /data-api ----
        data::DatabaseConfig dbConfig;
        dbConfig.postgresUrl = env("CRAMBERRY_DATABASE_URL");
        dbConfig.sqlitePath = env("CRAMBERRY_SQLITE_PATH", "cramberry.db");
        const auto db = data::Database::open(dbConfig);
        LOG_INFO << "Database: " << (db->kind() == data::Database::Kind::Postgres ? "PostgreSQL" : "SQLite " + dbConfig.sqlitePath);

        // ---- repositories ----
        SqlUserRepository users(*db);
        SqlTaskRepository tasks(*db);
        SqlScheduleRepository schedules(*db);
        SqlSettingsRepository settings(*db);
        SqlNotificationRepository notificationSettings(*db);
        SqlInbox inbox(*db);
        SqlAgentRepository agentRuns(*db);

        // ---- AI ----
        // OPENROUTER_API_KEY wins if both keys are set.
        std::unique_ptr<GeminiClient> gemini;
        std::string aiEngine;  // shown as "engine" on plans: the model ID without its provider
        if (const auto key = env("OPENROUTER_API_KEY"); !key.empty()) {
            auto config = GeminiConfig::openRouter(key, env("OPENROUTER_MODEL", "anthropic/claude-haiku-5.5"));
            config.reasoning = env("OPENROUTER_REASONING", "0") == "1";
            gemini = std::make_unique<GeminiClient>(config);
            aiEngine = config.model.substr(config.model.find('/') + 1);  // "anthropic/claude-haiku-5.5" -> "claude-haiku-5.5"
            LOG_INFO << "AI: " << config.model << " via OpenRouter";
        } else if (const auto key = env("GEMINI_API_KEY"); !key.empty()) {
            GeminiConfig config;
            config.apiKey = key;
            config.model = env("GEMINI_MODEL", config.model);
            gemini = std::make_unique<GeminiClient>(config);
            aiEngine = config.model;
            LOG_INFO << "AI: " << config.model << " via the Gemini API";
        } else {
            LOG_WARN << "Neither OPENROUTER_API_KEY nor GEMINI_API_KEY is set: schedules use the local planner and chat is off";
        }

        // Slow work (Gemini, password hashing) runs here, off Drogon's I/O threads.
        trantor::ConcurrentTaskQueue slowWork(static_cast<std::size_t>(envInt("CRAMBERRY_WORKERS", 4)), "work");

        // ---- controllers ----
        UserController userController(users, envInt("CRAMBERRY_PASSWORD_ITERATIONS",
                                                     crypto::kDefaultPasswordIterations));
        AuthGuard guard(userController, slowWork);
        TaskController taskController(tasks);
        AgentController agent(users, tasks, settings, schedules, agentRuns, gemini.get(), gemini.get(), aiEngine);
        BackgroundRegenerator regenerator(agent, slowWork);
        SettingsController settingsController(settings, schedules, regenerator);
        NotificationController notificationController(notificationSettings);
        ScheduleController scheduleController(schedules, settings, tasks, users);

        // ---- routes ----
        auto& app = drogon::app();
        const bool autoReplan = env("CRAMBERRY_AUTO_REPLAN", "1") == "1";
        registerUserRoutes(app, userController, guard, slowWork);
        registerTaskRoutes(app, taskController, guard,
                           autoReplan ? TasksChanged([&regenerator](const std::string& id) { regenerator.request(id); })
                                      : TasksChanged{});
        registerScheduleRoutes(app, scheduleController, guard);
        registerAgentRoutes(app, agent, guard);
        registerSettingsRoutes(app, settingsController, settings, guard);
        registerNotificationRoutes(app, notificationController, notificationSettings, inbox, guard);

        app.registerHandler("/api/health", http::open([&db, &agent](const http::HttpRequestPtr&) {
                                Json::Value v;
                                const bool dbOk = db->ping();
                                v["status"] = dbOk ? "ok" : "degraded";
                                v["database"] = dbOk;
                                v["ai"] = agent.aiConfigured();
                                return http::json(v, dbOk ? drogon::k200OK : drogon::k503ServiceUnavailable);
                            }),
                            {drogon::Get});

        app.setCustom404Page(http::error(drogon::k404NotFound, "No such endpoint"), false);
        app.registerPostHandlingAdvice([](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& r) {
            r->addHeader("X-Content-Type-Options", "nosniff");
            r->addHeader("Cache-Control", "no-store");
        });
        enableCors(env("CRAMBERRY_CORS_ORIGIN"));

        // ---- UC-16 deadline reminders, checked once a minute on their own thread ----
        SqlDeadlineSource deadlines(*db);
        InboxGateway gateway(inbox);
        ReminderScheduler reminders(notificationSettings, deadlines, gateway);
        trantor::EventLoopThread reminderThread("reminders");
        reminderThread.run();
        reminderThread.getLoop()->runEvery(60.0, [&reminders] {
            try {
                reminders.tick(json::nowMinutes());
            } catch (const std::exception& e) {
                LOG_ERROR << "Reminder check failed: " << e.what();
            }
        });

        const std::string host = env("CRAMBERRY_HOST", "127.0.0.1");
        const int port = envInt("CRAMBERRY_PORT", 8080);
        LOG_INFO << "Cramberry API listening on http://" << host << ":" << port;
        app.setClientMaxBodySize(256 * 1024)
            .setThreadNum(static_cast<std::size_t>(envInt("CRAMBERRY_THREADS", 4)))
            .addListener(host, static_cast<uint16_t>(port))
            .run();
    } catch (const std::exception& e) {
        std::cerr << "Cramberry failed to start: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
