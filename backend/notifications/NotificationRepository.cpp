#include "notifications/NotificationRepository.h"

#include <drogon/orm/Exception.h>
#include <trantor/utils/Logger.h>

#include "common/Json.h"
#include "user/Crypto.h"

namespace cramberry {

std::optional<NotificationSettings> SqlNotificationRepository::find(const std::string& studentId) const {
    const auto rows = db_.exec(
        "SELECT enabled, email, desktop, sms, phone_number, deadline_reminders, "
        "deadline_lead_minutes FROM notification_settings WHERE student_id = $1",
        studentId);
    if (rows.empty()) return std::nullopt;
    const auto& row = rows[0];
    NotificationSettings s;
    s.enabled = row["enabled"].as<int>() != 0;
    s.email = row["email"].as<int>() != 0;
    s.desktop = row["desktop"].as<int>() != 0;
    s.sms = row["sms"].as<int>() != 0;
    s.phoneNumber = row["phone_number"].as<std::string>();
    s.deadlineReminders = row["deadline_reminders"].as<int>() != 0;
    s.deadlineLeadMinutes = row["deadline_lead_minutes"].as<int>();
    return s;
}

bool SqlNotificationRepository::save(const std::string& studentId, const NotificationSettings& s) {
    try {
        db_.exec(
            "INSERT INTO notification_settings (student_id, enabled, email, desktop, sms, "
            "phone_number, deadline_reminders, deadline_lead_minutes, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
            "ON CONFLICT (student_id) DO UPDATE SET enabled = excluded.enabled, "
            "email = excluded.email, desktop = excluded.desktop, sms = excluded.sms, "
            "phone_number = excluded.phone_number, deadline_reminders = excluded.deadline_reminders, "
            "deadline_lead_minutes = excluded.deadline_lead_minutes, updated_at = excluded.updated_at",
            studentId, static_cast<int>(s.enabled), static_cast<int>(s.email),
            static_cast<int>(s.desktop), static_cast<int>(s.sms), s.phoneNumber,
            static_cast<int>(s.deadlineReminders), s.deadlineLeadMinutes,
            static_cast<std::int64_t>(json::nowMinutes()));
        return true;
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Saving notification settings failed: " << e.base().what();
        return false;
    }
}

std::string SqlInbox::add(const OutgoingMessage& m) {
    const std::string id = crypto::newId();
    db_.exec(
        "INSERT INTO notifications (id, student_id, channel, subject, body, sent_at, is_read) "
        "VALUES ($1, $2, $3, $4, $5, $6, 0)",
        id, m.studentId, json::channelName(m.channel), m.subject, m.body,
        static_cast<std::int64_t>(m.sentAt));
    return id;
}

std::vector<InboxItem> SqlInbox::list(const std::string& studentId, bool unreadOnly, int limit) const {
    std::vector<InboxItem> items;
    const auto rows = db_.exec(
        "SELECT id, channel, subject, body, sent_at, is_read FROM notifications "
        "WHERE student_id = $1 AND (is_read = 0 OR $2 = 0) ORDER BY sent_at DESC LIMIT $3",
        studentId, static_cast<int>(unreadOnly), limit);
    for (const auto& row : rows) {
        InboxItem item;
        item.id = row["id"].as<std::string>();
        item.channel = json::parseChannel(row["channel"].as<std::string>()).value_or(Channel::Desktop);
        item.subject = row["subject"].as<std::string>();
        item.body = row["body"].as<std::string>();
        item.sentAt = row["sent_at"].as<std::int64_t>();
        item.read = row["is_read"].as<int>() != 0;
        items.push_back(std::move(item));
    }
    return items;
}

bool SqlInbox::markRead(const std::string& studentId, const std::string& id) {
    return db_.exec("UPDATE notifications SET is_read = 1 WHERE student_id = $1 AND id = $2",
                    studentId, id)
               .affectedRows() > 0;
}

void SqlInbox::markAllRead(const std::string& studentId) {
    db_.exec("UPDATE notifications SET is_read = 1 WHERE student_id = $1", studentId);
}

void InboxGateway::send(const OutgoingMessage& message) {
    inbox_.add(message);
    if (message.channel == Channel::Desktop) return;
    if (deliver_) {
        deliver_(message);
    } else {
        LOG_INFO << "No " << json::channelName(message.channel)
                 << " provider configured; reminder kept in the in-app inbox only";
    }
}

}  // namespace cramberry
