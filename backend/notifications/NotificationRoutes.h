#pragma once

#include <drogon/HttpAppFramework.h>

#include "notifications/NotificationController.h"
#include "notifications/NotificationRepository.h"
#include "user/AuthGuard.h"

namespace cramberry {

// UC-16 Update Notification Settings and the in-app inbox
// GET  /api/settings/notifications
// PUT  /api/settings/notifications  {enabled, email, desktop, sms, phoneNumber,
//                                    deadlineReminders, deadlineLeadMinutes}
// GET  /api/notifications?unread=true
// POST /api/notifications/{id}/read
// POST /api/notifications/read-all
void registerNotificationRoutes(drogon::HttpAppFramework& app, NotificationController& controller,
                                const INotificationRepository& settings, SqlInbox& inbox,
                                const AuthGuard& guard);

}  // namespace cramberry
