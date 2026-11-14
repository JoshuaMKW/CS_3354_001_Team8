#include "notifications/NotificationController.h"

#include <cctype>

namespace cramberry {

bool isValidPhoneNumber(const std::string& phone) {
    std::size_t i = (!phone.empty() && phone[0] == '+') ? 1 : 0;
    std::size_t digits = 0;
    for (; i < phone.size(); ++i) {
        const char c = phone[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++digits;
        } else if (c != ' ' && c != '-' && c != '(' && c != ')') {
            return false;
        }
    }
    return digits >= 10 && digits <= 15;  // E.164 allows at most 15 digits
}

NotificationController::NotificationController(INotificationRepository& repository)
    : repository_(repository) {}

std::optional<std::string> NotificationController::validatePreferences(
    const NotificationSettings& settings) {
    if (settings.sms && !isValidPhoneNumber(settings.phoneNumber)) {
        return "Enter a valid phone number";
    }
    if (settings.deadlineReminders && settings.deadlineLeadMinutes <= 0) {
        return "Choose when to be reminded before a deadline";
    }
    if (settings.enabled && !settings.email && !settings.desktop && !settings.sms) {
        return "Turn on at least one notification source";
    }
    return std::nullopt;
}

UpdateResult NotificationController::updateSettings(const std::string& studentId,
                                                    const NotificationSettings& settings) {
    if (auto error = validatePreferences(settings)) {
        return {false, *error};
    }
    if (!repository_.save(studentId, settings)) {
        return {false, "Couldn't save your notification settings. Please try again."};
    }
    return {true, "Notification settings saved."};
}

}  // namespace cramberry
