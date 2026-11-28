#pragma once

#include <trantor/utils/ConcurrentTaskQueue.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "agent/AgentRepository.h"
#include "agent/GeminiClient.h"
#include "common/Outcome.h"
#include "schedule/Schedule.h"
#include "settings/SettingsController.h"
#include "tasks/Task.h"
#include "user/User.h"

namespace cramberry {

struct GeneratedPlan {
    std::string engine;  // the AI model that made it, e.g. "claude-haiku-5.5", or "local"
    std::vector<StudyBlock> blocks;
    std::vector<std::string> warnings;
};

// UC-5 Generate AI Study Schedule, UC-7 Interact with AI Chatbot, UC-12 Validate AI
// Response. Gemini is tried first; if it isn't configured, fails, or proposes a
// schedule that breaks the student's preferences, the local planner is used instead
// (NFR-15). Only a schedule that passes every check is saved.
class AgentController {
public:
    AgentController(const IUserRepository& users, const ITaskRepository& tasks,
                    const ISettingsRepository& settings, IScheduleRepository& schedules,
                    IAgentRepository& runs, IAiClient* gemini, IChatModel* chat,
                    std::string aiEngine = "gemini");

    // Blocks the calling thread while Gemini works; call from a worker thread.
    Outcome<GeneratedPlan> generate(const std::string& studentId, std::optional<Day> onlyDay,
                                    Timestamp now);
    Outcome<std::string> chat(const std::string& studentId, const std::string& message, Timestamp now);
    std::vector<GenerationRun> history(const std::string& studentId) const;

    bool aiConfigured() const { return gemini_ != nullptr; }

private:
    const IUserRepository& users_;
    const ITaskRepository& tasks_;
    const ISettingsRepository& settings_;
    IScheduleRepository& schedules_;
    IAgentRepository& runs_;
    IAiClient* gemini_;
    IChatModel* chat_;
    std::string aiEngine_;  // label for plans and runs made by gemini_
};

// How a background re-plan asked for with a ticket turned out.
struct ReplanStatus {
    enum class State { Pending, Done, Skipped, Failed, Unknown };
    State state = State::Unknown;
    std::string engine;                // Done: who made the plan, e.g. "claude-haiku-5.5" or "local"
    std::string message;               // Skipped or Failed: why
    std::vector<std::string> details;  // Done: warnings; Failed: the planner's reasons
};

std::string toString(ReplanStatus::State state);

// "Plan Updating" from the SOW: rebuilds a student's schedule in the background after
// their preferences (UC-15) or tasks change. Requests for a student who already has
// one waiting are merged, so a burst of edits costs one generation, and a student never
// has two re-plans running at once, so an older plan can't overwrite a newer one.
//
// request() returns a ticket. status() answers for that ticket once a re-plan that
// started after it has finished, so a client can wait for exactly its own change.
class BackgroundRegenerator : public IScheduleRegenerator {
public:
    BackgroundRegenerator(AgentController& agent, trantor::ConcurrentTaskQueue& queue)
        : agent_(agent), queue_(queue) {}

    void regenerate(const std::string& studentId, const StudyPreferences& prefs) override;
    std::uint64_t request(const std::string& studentId);
    ReplanStatus status(const std::string& studentId, std::uint64_t ticket) const;

private:
    struct StudentState {
        std::uint64_t issued = 0;    // last ticket handed out
        std::uint64_t finished = 0;  // every ticket up to this one has a finished re-plan
        bool queued = false;         // a job is waiting in the queue
        bool running = false;        // a job is generating now
        ReplanStatus last;           // outcome of the re-plan that set `finished`
    };

    void run(const std::string& studentId);

    AgentController& agent_;
    trantor::ConcurrentTaskQueue& queue_;
    mutable std::mutex mutex_;
    std::map<std::string, StudentState> students_;
};

}  // namespace cramberry
