#pragma once

#include <drogon/HttpAppFramework.h>

#include "agent/AgentController.h"
#include "user/AuthGuard.h"

namespace cramberry {

// POST /api/agent/generate  {day?: "Mon"}  -> {engine, schedule, warnings}   (UC-5, FR-16)
// POST /api/agent/chat      {message}      -> {reply}                         (UC-7, FR-20)
// GET  /api/agent/runs                     -> recent generation attempts       (FR-29)
void registerAgentRoutes(drogon::HttpAppFramework& app, AgentController& agent, const AuthGuard& guard);

}  // namespace cramberry
