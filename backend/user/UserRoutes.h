#pragma once

#include <drogon/HttpAppFramework.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include "user/AuthGuard.h"
#include "user/UserController.h"

namespace cramberry {

// POST   /api/auth/register   {name, email, password, utcOffsetMinutes?}
// POST   /api/auth/login      {email, password}
// POST   /api/auth/logout
// GET    /api/users/me
// PATCH  /api/users/me        {name?, utcOffsetMinutes?}
// DELETE /api/users/me        {password}
void registerUserRoutes(drogon::HttpAppFramework& app, UserController& users,
                        const AuthGuard& guard, trantor::ConcurrentTaskQueue& slowWork);

Json::Value toJson(const Student& student);

}  // namespace cramberry
