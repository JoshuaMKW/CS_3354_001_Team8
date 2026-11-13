#pragma once

#include <optional>
#include <string>

#include "notifications/Notification.h"

namespace cramberry {

struct UpdateResult {
    bool ok = false;
    std::string message;
};

bool isValidPhoneNumber(const std::string& phone);

// UC-16 Update Notification Settings. Follows the JA sequence diagram:
// Update Notifications -> ValidatePreferences() ->
//   [validated] Transact(acc) -> ok -> Confirmation
//   [exception] Show Error (nothing saved)
class NotificationController {
public:
    explicit NotificationController(INotificationRepository& repository);

    UpdateResult updateSettings(const std::string& studentId, const NotificationSettings& settings);

    // Returns the error message to show, or nothing if the settings are valid.
    static std::optional<std::string> validatePreferences(const NotificationSettings& settings);

private:
    INotificationRepository& repository_;
};

}  // namespace cramberry
