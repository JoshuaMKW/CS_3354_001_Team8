#pragma once

#include <functional>
#include <string>
#include <vector>

#include "data-api/Database.h"
#include "notifications/Notification.h"

namespace cramberry {

// UC-16 notification settings, one row per student.
class SqlNotificationRepository : public INotificationRepository {
public:
    explicit SqlNotificationRepository(const data::Database& db) : db_(db) {}

    std::optional<NotificationSettings> find(const std::string& studentId) const override;
    bool save(const std::string& studentId, const NotificationSettings& settings) override;

private:
    const data::Database& db_;
};

// Notification in the UML class diagram: a message the student can read in the app.
struct InboxItem {
    std::string id;
    Channel channel = Channel::Desktop;
    std::string subject;
    std::string body;
    Timestamp sentAt = 0;
    bool read = false;
};

class SqlInbox {
public:
    explicit SqlInbox(const data::Database& db) : db_(db) {}

    std::string add(const OutgoingMessage& message);
    std::vector<InboxItem> list(const std::string& studentId, bool unreadOnly, int limit) const;
    bool markRead(const std::string& studentId, const std::string& id);
    void markAllRead(const std::string& studentId);

private:
    const data::Database& db_;
};

// The Notification Service gateway used by ReminderScheduler. Every reminder is kept
// in the student's inbox so the app can show it; email and SMS are also handed to
// `deliver`, which is where a provider such as SendGrid or Twilio plugs in. Without
// one they are only logged.
class InboxGateway : public INotificationGateway {
public:
    using Deliver = std::function<void(const OutgoingMessage&)>;

    InboxGateway(SqlInbox& inbox, Deliver deliver = {})
        : inbox_(inbox), deliver_(std::move(deliver)) {}

    void send(const OutgoingMessage& message) override;

private:
    SqlInbox& inbox_;
    Deliver deliver_;
};

}  // namespace cramberry
