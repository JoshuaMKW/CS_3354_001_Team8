#pragma once

#include <drogon/HttpAppFramework.h>

#include "schedule/ScheduleSharing.h"
#include "user/AuthGuard.h"

namespace cramberry {

// POST   /api/schedule/shares             {email}
// GET    /api/schedule/shares
// DELETE /api/schedule/shares/{shareId}
// GET    /api/schedule/shared-with-me
// GET    /api/schedule/shared-with-me/{shareId}
void registerScheduleSharingRoutes(drogon::HttpAppFramework& app,
                                   ScheduleSharingController& sharing,
                                   const AuthGuard& guard);

}  // namespace cramberry
