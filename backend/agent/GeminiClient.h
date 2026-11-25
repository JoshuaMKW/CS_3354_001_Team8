#pragma once

#include <drogon/HttpClient.h>
#include <json/json.h>
#include <trantor/net/EventLoopThread.h>

#include <string>
#include <utility>
#include <vector>

#include "agent/Agent.h"

namespace cramberry {

struct GeminiConfig {
    // Which API carries the request. OpenRouter serves many providers (Claude, GPT, Gemini) and uses the
    // OpenAI-style chat-completions format and model IDs such as "anthropic/claude-haiku-5.5".
    enum class Api { Gemini, OpenRouter };

    Api api = Api::Gemini;
    std::string apiKey;
    std::string model = "gemini-2.5-flash";
    std::string host = "https://generativelanguage.googleapis.com";
    double timeoutSeconds = 10.0;  // NFR-6 / NFR-12: answer within 10 seconds or give up
    // OpenRouter only. Off by default: Claude Haiku 5.5 reasons unless told not to, which
    // measured 6.7-10.6 s per schedule (past the timeout) against 4.2-4.6 s without.
    bool reasoning = false;

    static GeminiConfig openRouter(std::string apiKey, std::string model = "anthropic/claude-haiku-5.5") {
        GeminiConfig config;
        config.api = Api::OpenRouter;
        config.apiKey = std::move(apiKey);
        config.model = std::move(model);
        config.host = "https://openrouter.ai";
        return config;
    }
};

// FR-20: answers a student's question about their own tasks and schedule.
class IChatModel {
public:
    virtual ~IChatModel() = default;
    virtual std::string reply(const std::string& context, const std::string& message) = 0;
};

// The Gemini API (generativelanguage.googleapis.com). Calls block the calling thread,
// so only call it from a worker thread, never from a Drogon I/O thread. Every method
// throws std::runtime_error on network errors, timeouts or unusable answers.
class GeminiClient : public IAiClient, public IChatModel {
public:
    explicit GeminiClient(GeminiConfig config);

    std::vector<StudyBlock> proposeSchedule(const StudyPreferences& prefs,
                                            const std::vector<Assignment>& assignments) override;
    std::string reply(const std::string& context, const std::string& message) override;

    // Exposed for unit tests.
    static std::string schedulePrompt(const StudyPreferences& prefs,
                                      const std::vector<Assignment>& assignments);
    static std::vector<StudyBlock> parseSchedule(const std::string& modelText);
    // The JSON body sent to the configured API, and the model's text pulled out of its reply.
    static Json::Value requestBody(const GeminiConfig& config, const std::string& systemInstruction,
                                   const std::string& userText, bool wantJson);
    static std::string replyText(const GeminiConfig& config, const Json::Value& reply);

private:
    // Sends one generateContent request and returns the model's text.
    std::string generate(const std::string& systemInstruction, const std::string& userText,
                         bool wantJson);

    GeminiConfig config_;
    trantor::EventLoopThread loopThread_;  // the HTTP clients' own loop, so sync calls can't deadlock
    // No shared HttpClient: one Drogon client sends one request at a time, so a
    // background re-plan and a student's request would queue behind each other and the
    // second would hit the timeout. generate() opens a client per call instead.
};

}  // namespace cramberry
