#include "notifications/ReminderScheduler.h"

namespace cramberry {

ReminderScheduler::ReminderScheduler(const INotificationRepository& settings,
                                     const ITaskSource& tasks, INotificationGateway& gateway)
    : settings_(settings), tasks_(tasks), gateway_(gateway) {}

void ReminderScheduler::tick(Timestamp now) {
    for (const auto& item : tasks_.upcomingDeadlines(now)) {
        const auto settings = settings_.find(item.studentId);
        if (!settings || !settings->enabled || !settings->deadlineReminders) continue;

        const Timestamp sendAt = item.due - settings->deadlineLeadMinutes;
        if (now < sendAt || now >= item.due) continue;

        std::vector<Channel> channels;
        if (settings->email) channels.push_back(Channel::Email);
        if (settings->desktop) channels.push_back(Channel::Desktop);
        if (settings->sms) channels.push_back(Channel::Sms);

        for (const Channel channel : channels) {
            if (!sent_.emplace(item.studentId, item.assignmentId, channel).second) continue;
            gateway_.send({item.studentId, channel, "Reminder: " + item.title + " is due soon",
                           item.title + " is due " + formatTimestamp(item.due) + ".", now});
        }
    }
}

}  // namespace cramberry
