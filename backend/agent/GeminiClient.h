#pragma once

#include <drogon/HttpClient.h>
#include <json/json.h>
#include <trantor/net/EventLoopThread.h>

#include <string>
#include <vector>

#include "agent/Agent.h"

namespace cramberry {

struct GeminiConfig {
    std::string apiKey;
    std::string model = "gemini-2.5-flash";
    std::string host = "https://generativelanguage.googleapis.com";
    double timeoutSeconds = 10.0;  // NFR-6 / NFR-12: answer within 10 seconds or give up
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

private:
    // Sends one generateContent request and returns the model's text.
    std::string generate(const std::string& systemInstruction, const std::string& userText,
                         bool wantJson);

    GeminiConfig config_;
    trantor::EventLoopThread loopThread_;  // the HTTP client's own loop, so sync calls can't deadlock
    drogon::HttpClientPtr client_;
};

}  // namespace cramberry
