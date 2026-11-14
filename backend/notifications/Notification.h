#pragma once

#include <optional>
#include <string>

#include "common/Time.h"

namespace cramberry {

// UC-16 Update Notification Settings (Account -> Notifications page).
struct NotificationSettings {
    bool enabled = false;  // "Enable Notifications" master switch
    bool email = false;
    bool desktop = false;
    bool sms = false;
    std::string phoneNumber;
    bool deadlineReminders = false;
    int deadlineLeadMinutes = 24 * 60;  // "Deadline Reminders: 1 Day"

    bool operator==(const NotificationSettings&) const = default;
};

class INotificationRepository {
public:
    virtual ~INotificationRepository() = default;
    virtual std::optional<NotificationSettings> find(const std::string& studentId) const = 0;
    // Returns false if the write failed; the stored row must then be left unchanged.
    virtual bool save(const std::string& studentId, const NotificationSettings& settings) = 0;
};

enum class Channel { Email, Desktop, Sms };

struct OutgoingMessage {
    std::string studentId;
    Channel channel{};
    std::string subject;
    std::string body;
    Timestamp sentAt = 0;
};

// Delivers a message through the external Notification Service (email, push, SMS).
class INotificationGateway {
public:
    virtual ~INotificationGateway() = default;
    virtual void send(const OutgoingMessage& message) = 0;
};

}  // namespace cramberry
