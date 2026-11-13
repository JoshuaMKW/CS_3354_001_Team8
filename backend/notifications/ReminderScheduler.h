#pragma once

#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "notifications/Notification.h"

namespace cramberry {

struct DeadlineItem {
    std::string studentId;
    std::string assignmentId;
    std::string title;
    Timestamp due = 0;
};

// Source of upcoming assignment deadlines (Task Management System).
class ITaskSource {
public:
    virtual ~ITaskSource() = default;
    virtual std::vector<DeadlineItem> upcomingDeadlines(Timestamp now) const = 0;
};

// Trigger Notification Reminders: run tick() once a minute (e.g. from a cron job).
// Sends each deadline reminder once, on every channel the student turned on.
class ReminderScheduler {
public:
    ReminderScheduler(const INotificationRepository& settings, const ITaskSource& tasks,
                      INotificationGateway& gateway);

    void tick(Timestamp now);

private:
    const INotificationRepository& settings_;
    const ITaskSource& tasks_;
    INotificationGateway& gateway_;
    std::set<std::tuple<std::string, std::string, Channel>> sent_;
};

}  // namespace cramberry
