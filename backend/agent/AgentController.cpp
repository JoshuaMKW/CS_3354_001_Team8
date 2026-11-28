#include "agent/AgentController.h"

#include <trantor/utils/Logger.h>

#include <map>

#include "agent/NKStudyScheduleService.h"
#include "agent/Planning.h"
#include "common/Json.h"
#include "user/Crypto.h"

namespace cramberry {

namespace {

constexpr std::size_t kMaxChatMessage = 2000;

// Keeps the real reason Gemini failed; NKStudyScheduleService only reports a generic one.
class RecordingAi : public IAiClient {
public:
    explicit RecordingAi(IAiClient& inner) : inner_(inner) {}

    std::vector<StudyBlock> proposeSchedule(const StudyPreferences& prefs,
                                            const std::vector<Assignment>& assignments) override {
        try {
            return inner_.proposeSchedule(prefs, assignments);
        } catch (const std::exception& e) {
            error = e.what();
            LOG_WARN << "AI schedule generation failed: " << e.what();
            throw;
        }
    }

    std::string error;

private:
    IAiClient& inner_;
};

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

}  // namespace

AgentController::AgentController(const IUserRepository& users, const ITaskRepository& tasks,
                                 const ISettingsRepository& settings, IScheduleRepository& schedules,
                                 IAgentRepository& runs, IAiClient* gemini, IChatModel* chat,
                                 std::string aiEngine)
    : users_(users),
      tasks_(tasks),
      settings_(settings),
      schedules_(schedules),
      runs_(runs),
      gemini_(gemini),
      chat_(chat),
      aiEngine_(std::move(aiEngine)) {}

Outcome<GeneratedPlan> AgentController::generate(const std::string& studentId,
                                                 std::optional<Day> onlyDay, Timestamp now) {
    const auto prefs = settings_.find(studentId);
    if (!prefs) {
        return Error{ErrorCode::Unprocessable, "Set your study preferences before generating a schedule", {}};
    }
    const auto student = users_.findById(studentId);
    if (!student) return notFound("Account not found");

    const PlanningWeek week = planningWeek(now, student->utcOffsetMinutes);
    const auto assignments = plannerAssignments(tasks_.listTasks(studentId, {}), week, now);
    if (assignments.empty()) {
        return Error{ErrorCode::Unprocessable, "You have no unfinished tasks due in the next 7 days", {}};
    }
    // Only the hours still ahead this week, so neither the AI nor the local planner
    // schedules study time that has already passed.
    const StudyPreferences remaining = remainingStudyHours(*prefs, week, now);
    if (remaining.studyHours.empty()) {
        return Error{ErrorCode::Unprocessable,
                     "None of your study hours are left this week. Add more study hours or try again on Monday.",
                     {}};
    }

    std::vector<std::string> warnings;
    auto attempt = [&](IAiClient& ai, const std::string& engine) -> std::optional<GeneratedPlan> {
        Agent agent(ai);
        NKStudyScheduleService service(agent, schedules_);
        const auto result = service.generate({studentId, remaining, assignments, onlyDay});
        runs_.record({crypto::newId(), studentId, now, engine, result.ok,
                      static_cast<int>(result.schedule.size()), result.errors});
        if (result.ok) return GeneratedPlan{engine, result.schedule, {}};
        for (const auto& e : result.errors) warnings.push_back(engine + ": " + e);
        return std::nullopt;
    };

    if (gemini_) {
        RecordingAi gemini(*gemini_);
        if (auto plan = attempt(gemini, aiEngine_)) return std::move(*plan);
        if (!gemini.error.empty()) warnings.push_back(aiEngine_ + ": " + gemini.error);
    }

    LocalPlanner planner;
    auto plan = attempt(planner, "local");
    warnings.insert(warnings.end(), planner.shortfalls().begin(), planner.shortfalls().end());
    if (!plan) {
        return Error{ErrorCode::Unprocessable,
                     "Couldn't fit your tasks into your study hours. Try adding study hours, "
                     "raising the daily maximum or shortening estimates.",
                     warnings};
    }
    plan->warnings = std::move(warnings);
    return std::move(*plan);
}

Outcome<std::string> AgentController::chat(const std::string& studentId, const std::string& message,
                                           Timestamp now) {
    if (!chat_) {
        return Error{ErrorCode::Unavailable, "The AI assistant isn't set up on this server", {}};
    }
    const std::string question = trim(message);
    if (question.empty()) return invalid("Type a question first");
    if (question.size() > kMaxChatMessage) return invalid("Questions must be 2000 characters or fewer");

    const auto student = users_.findById(studentId);
    if (!student) return notFound("Account not found");

    std::map<std::string, std::string> courseNames;
    for (const auto& c : tasks_.listCourses(studentId)) {
        courseNames[c.id] = c.code.empty() ? c.name : c.code;
    }
    std::string context = "Student: " + student->name + "\nNow: " + json::formatIsoTimestamp(now) +
                          "\nTasks (status, title, course, due, estimate, priority 1=high):\n";
    for (const auto& t : tasks_.listTasks(studentId, {})) {
        if (t.status == TaskStatus::Completed && t.dueAt < now - 7 * kMinutesPerDay) continue;
        context += "- [" + toString(t.status) + "] " + t.title;
        if (auto it = courseNames.find(t.courseId); it != courseNames.end()) context += " (" + it->second + ")";
        context += ", " + toString(t.type) + ", due " + json::formatIsoTimestamp(t.dueAt) + ", " +
                   std::to_string(t.estimatedMinutes) + " min, priority " + std::to_string(t.priority) +
                   (t.isOverdue(now) ? ", OVERDUE" : "") + "\n";
    }
    context += "This week's study schedule (student's local time):\n";
    for (const auto& b : schedules_.getActiveStudyItems(studentId)) {
        context += "- " + toString(b.time) + ": " + b.title + "\n";
    }

    try {
        return chat_->reply(context, question);
    } catch (const std::exception& e) {
        LOG_WARN << "AI chat failed: " << e.what();
        return Error{ErrorCode::Unavailable,
                     "The AI assistant is unavailable right now. Your tasks and schedule are unaffected.",
                     {}};
    }
}

std::vector<GenerationRun> AgentController::history(const std::string& studentId) const {
    return runs_.recent(studentId, 20);
}

std::string toString(ReplanStatus::State state) {
    switch (state) {
        case ReplanStatus::State::Pending: return "pending";
        case ReplanStatus::State::Done: return "done";
        case ReplanStatus::State::Skipped: return "skipped";
        case ReplanStatus::State::Failed: return "failed";
        case ReplanStatus::State::Unknown: break;
    }
    return "unknown";
}

void BackgroundRegenerator::regenerate(const std::string& studentId, const StudyPreferences&) {
    request(studentId);
}

std::uint64_t BackgroundRegenerator::request(const std::string& studentId) {
    std::lock_guard lock(mutex_);
    auto& st = students_[studentId];
    const auto ticket = ++st.issued;
    // A running job picks this up when it finishes; a queued one hasn't read the tasks yet.
    if (!st.queued && !st.running) {
        st.queued = true;
        queue_.runTaskInQueue([this, studentId] { run(studentId); });
    }
    return ticket;
}

void BackgroundRegenerator::run(const std::string& studentId) {
    std::uint64_t covers = 0;
    {
        std::lock_guard lock(mutex_);
        auto& st = students_[studentId];
        st.queued = false;
        st.running = true;
        covers = st.issued;  // every change made before now is in the data this run reads
    }
    while (true) {
        ReplanStatus outcome;
        try {
            const auto result = agent_.generate(studentId, std::nullopt, json::nowMinutes());
            if (result) {
                outcome.state = ReplanStatus::State::Done;
                outcome.engine = result.value().engine;
                outcome.details = result.value().warnings;
            } else {
                const auto& error = result.error();
                outcome.state = error.code == ErrorCode::Unprocessable ? ReplanStatus::State::Skipped
                                                                       : ReplanStatus::State::Failed;
                outcome.message = error.message;
                outcome.details = error.details;
                LOG_INFO << "Background re-plan for " << studentId << " "
                         << toString(outcome.state) << ": " << error.message;
            }
        } catch (const std::exception& e) {
            outcome.state = ReplanStatus::State::Failed;
            outcome.message = "The planner couldn't re-plan your week.";
            LOG_ERROR << "Background re-plan failed: " << e.what();
        }

        std::lock_guard lock(mutex_);
        auto& st = students_[studentId];
        st.finished = covers;
        st.last = std::move(outcome);
        if (st.issued == covers) {
            st.running = false;
            return;
        }
        covers = st.issued;  // more changes arrived while this one ran: plan again with them
    }
}

ReplanStatus BackgroundRegenerator::status(const std::string& studentId, std::uint64_t ticket) const {
    std::lock_guard lock(mutex_);
    const auto it = students_.find(studentId);
    if (it == students_.end() || ticket == 0 || ticket > it->second.issued) return {};
    if (ticket <= it->second.finished) return it->second.last;
    ReplanStatus pending;
    pending.state = ReplanStatus::State::Pending;
    return pending;
}

}  // namespace cramberry
