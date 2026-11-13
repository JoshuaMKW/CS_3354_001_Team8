#pragma once

// In-memory stand-ins for Supabase, Gemini and the Notification Service, so the
// tests run without network access and never touch a real student's data.

#include <map>
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "notifications/Notification.h"
#include "notifications/ReminderScheduler.h"
#include "schedule/Schedule.h"
#include "settings/SettingsController.h"

namespace cramberry::testing {

class InMemorySettingsRepository : public ISettingsRepository {
public:
    std::map<std::string, StudyPreferences> rows;
    int writes = 0;

    std::optional<StudyPreferences> find(const std::string& studentId) const override {
        auto it = rows.find(studentId);
        if (it == rows.end()) return std::nullopt;
        return it->second;
    }
    bool save(const std::string& studentId, const StudyPreferences& prefs) override {
        ++writes;
        rows[studentId] = prefs;
        return true;
    }
};

class InMemoryScheduleRepository : public IScheduleRepository {
public:
    std::map<std::string, std::vector<StudyBlock>> schedules;

    std::vector<StudyBlock> getActiveStudyItems(const std::string& studentId) const override {
        auto it = schedules.find(studentId);
        return it == schedules.end() ? std::vector<StudyBlock>{} : it->second;
    }
    void replace(const std::string& studentId, std::vector<StudyBlock> blocks) override {
        schedules[studentId] = std::move(blocks);
    }
};

class RecordingRegenerator : public IScheduleRegenerator {
public:
    std::vector<std::string> calls;  // student IDs, one per regenerate() call

    void regenerate(const std::string& studentId, const StudyPreferences&) override {
        calls.push_back(studentId);
    }
};

// Plays the part of Gemini by returning a fixed schedule.
class CannedAiClient : public IAiClient {
public:
    explicit CannedAiClient(std::vector<StudyBlock> reply) : reply_(std::move(reply)) {}

    std::vector<StudyBlock> proposeSchedule(const StudyPreferences&,
                                            const std::vector<Assignment>&) override {
        return reply_;
    }

private:
    std::vector<StudyBlock> reply_;
};

class InMemoryNotificationRepository : public INotificationRepository {
public:
    std::map<std::string, NotificationSettings> rows;
    int writes = 0;

    std::optional<NotificationSettings> find(const std::string& studentId) const override {
        auto it = rows.find(studentId);
        if (it == rows.end()) return std::nullopt;
        return it->second;
    }
    bool save(const std::string& studentId, const NotificationSettings& settings) override {
        ++writes;
        rows[studentId] = settings;
        return true;
    }
};

class FakeTaskSource : public ITaskSource {
public:
    std::vector<DeadlineItem> items;

    std::vector<DeadlineItem> upcomingDeadlines(Timestamp now) const override {
        std::vector<DeadlineItem> upcoming;
        for (const auto& item : items) {
            if (item.due > now) upcoming.push_back(item);
        }
        return upcoming;
    }
};

// Test inbox: records every message instead of sending it.
class RecordingGateway : public INotificationGateway {
public:
    std::vector<OutgoingMessage> sent;

    void send(const OutgoingMessage& message) override { sent.push_back(message); }
};

}  // namespace cramberry::testing
