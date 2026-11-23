#pragma once

#include <drogon/HttpAppFramework.h>

#include "schedule/ScheduleController.h"
#include "user/AuthGuard.h"

namespace cramberry {

// GET    /api/schedule
// PATCH  /api/schedule/blocks   {assignmentId, from: {day,start,end}, to: {day,start,end}, title?}
// DELETE /api/schedule
// GET    /api/schedule/export.ics
// GET    /api/dashboard
void registerScheduleRoutes(drogon::HttpAppFramework& app, ScheduleController& schedule,
                            const AuthGuard& guard);

}  // namespace cramberry
