# Assignment 5 – RM Implementation and Testing

Author: Risha Mothukuru (RM)
Project: Cramberry – AI Study Planner
Date: November 27, 2026

## Implementation Status

UC-13: Update Task Status
UC-14: View Weekly Progress Report

Both use cases are implemented in the integrated C++ Drogon backend.
Task status, completion timestamps, course/date filtering, overdue
tasks, and weekly progress reports are supported.

## Functional Test Results

Tester: Risha Mothukuru
Environment: macOS, AppleClang, CMake 4.4.3,
Drogon 1.9.14, GoogleTest, in-memory SQLite.

| ID | Requirement | Input | Expected Result | Actual Result | Status |
|---|---|---|---|---|---|
| TC-13-01 | UC-13 | Complete and reopen task | Status and completion timestamp update | Matched expected | Pass |
| TC-13-05 | FR-24 | Course, date and status filters | Only matching tasks returned | Matched expected | Pass |
| TC-13-06 | FR-26 | Past-due incomplete task | Task identified as overdue | Matched expected | Pass |
| TC-14-01 | UC-14 | Completed, overdue and upcoming tasks | Correct weekly categories | Matched expected | Pass |
| TC-RM-05 | Task validation | Invalid title, priority and estimate | Invalid inputs rejected | Matched expected | Pass |
| TC-RM-06 | Course management | Delete course containing tasks | Tasks preserved | Matched expected | Pass |

Automated task suite: 6/6 passed.
Integrated backend suite: 58/58 passed.

## Non-Functional Testing

NFR-15: AI Service Isolation

Simulated AI failure using the automated AgentTest suite.
The local planner remained functional, and an existing
schedule was retained when generation failed.

Result: Pass for simulated backend scenarios.

Production uptime, browser compatibility, 200% zoom,
and contrast requirements were not verified because
the complete deployed frontend was unavailable.

## User Guide

### Update Task Status

1. Sign in to Cramberry.
2. Locate the desired task.
3. Select Not Started, In Progress, or Completed.
4. Confirm the updated status.

Backend endpoint: PUT /api/tasks/{id}/status

### Weekly Progress Report

1. Sign in to Cramberry.
2. Request the weekly progress report.
3. Review completed, overdue, and remaining tasks.

Backend endpoint: GET /api/reports/weekly

The backend functionality was verified. Final frontend
navigation remains subject to UI integration.

## AI Usage

ChatGPT was used to assist with understanding the updated
backend architecture, configuring build dependencies,
reviewing test coverage, and drafting documentation.

Example prompt: How can I verify UC-13 and UC-14 against
the integrated C++ Drogon backend?

All reported results were verified by compiling the backend
and personally executing the automated tests.

## Test Execution

ctest --test-dir backend/out/rm-build --output-on-failure

## Working Code

https://github.com/JoshuaMKW/CS_3354_001_Team8