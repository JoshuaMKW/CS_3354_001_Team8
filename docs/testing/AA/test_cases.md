# Software Testing: Functional and Non-Functional Test Cases (AA)

**Project:** Cramberry (Group 8)
**Test author:** Ahnaf Adib (AA)
**Use cases covered:** UC-10 Process Scheduling Request, UC-11 Save/Retrieve Student Data, UC-12 Validate AI Response
**Requirements covered:** FR-27, FR-28, FR-29, NFR-18, NFR-19

IDs follow the team's `TC-<use case>-<n>` scheme. The Assignment 5 tests below run against the live application; the Assignment 4 unit tests after them use in-memory fakes. Each test case has the fields from the assignment: test case ID, purpose/scenario, preconditions, inputs and system conditions, test data, steps, expected result, creator, actual result and Pass/Fail/Not Run.

## Assignment 5: Tests Against the Running Application

These tests run against the real application: `cramberry_server` connected to Supabase, with Claude Haiku 5.5 generating schedules through OpenRouter. They call the HTTP API the same way the frontend does, then check the rows in Supabase directly. Every test uses the one test account (`student@cramberry.test`), whose data is cleared before each test, so no real data is touched and no extra users are left behind. TC-NFR19-02 needs a second student to try to reach the first one's data, so it creates a temporary one and deletes it when the test ends.

**Script:** [`backend/tests/live/aa_live_tests.py`](../../../backend/tests/live/aa_live_tests.py)
**Results:** [`results.txt`](results.txt)

### Test environment

| Item | Value |
| :--- | :--- |
| Date | Fri 27 Nov 2026, 17:53 UTC (11:53 AM for the test students, who are on UTC−6) |
| Machine | MacBook, Apple M5, macOS 27.0.1 |
| Server | `cramberry_server` Debug build on `http://127.0.0.1:8080`, `CRAMBERRY_AUTO_REPLAN=0` so task edits don't start background re-plans during a test |
| Database | Supabase hosted PostgreSQL, direct connection (`db.<project>.supabase.co:5432`) |
| AI | `anthropic/claude-haiku-5.5` through OpenRouter, reasoning off, 10 s timeout |
| Failure server (TC-12-03 only) | A second copy on port 8091 with `OPENROUTER_MODEL=anthropic/no-such-model`, so every AI call fails |
| Client | Python 3 `urllib`, one request at a time; `psycopg` for the database checks |

Study preferences used by every test that needs them: Mon–Fri 16:00–21:00, Sat–Sun 10:00–14:00, personal time Sat 12:00–13:00, 60 min sessions, 15 min breaks, at most 180 min a day.

To run them:

```bash
cd backend
set -a; source .env; set +a
./out/build/cramberry_server &                                    # main server
CRAMBERRY_PORT=8091 OPENROUTER_MODEL=anthropic/no-such-model ./out/build/cramberry_server &
pip install "psycopg[binary]"
BASE_BAD_AI=http://127.0.0.1:8091 python3 tests/live/aa_live_tests.py
```

### Result summary

| Test case | Type | Requirement | Result |
| :--- | :--- | :--- | :--- |
| TC-10-02 Valid Scheduling Request Is Planned | Functional | FR-27 | **Pass** |
| TC-10-03 Invalid Input Is Rejected | Functional | FR-27 | **Pass** |
| TC-10-04 Request Without Preferences Is Refused | Functional | FR-27 | **Pass** |
| TC-11-02 Data Is Saved, Updated and Read Back | Functional | FR-28 | **Pass** |
| TC-12-02 AI Schedule Is Validated and Saved | Functional | FR-29 | **Pass** |
| TC-12-03 AI Failure Falls Back to a Valid Plan | Functional | FR-29, NFR-15 | **Pass** |
| TC-NFR18-02 API Response Time on Supabase | Non-functional | NFR-18 | **Pass** |
| TC-NFR19-02 Students Can't Reach Each Other's Data | Non-functional | NFR-19 | **Pass** |

**8 run, 8 Pass, 0 Fail, 0 Not Run.**

### Functional test cases

#### TC-10-02: Valid Scheduling Request Is Planned

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Successful flow. A student with preferences and tasks asks for a plan, and the AI makes it. |
| **Requirement** | FR-27 |
| **Preconditions** | Student registered and signed in; preferences saved. |
| **Inputs and system conditions** | `POST /api/agent/generate` with an empty body. |
| **Test data** | Task 1 "CS 3354 Assignment 5 report", assignment, due in 2 days at 23:59 UTC, 180 min, priority 1. Task 2 "MATH 2418 Exam 3", exam, due in 4 days, 120 min, priority 2. |
| **Steps** | 1. Save preferences. 2. Create both tasks. 3. Call generate. 4. Check the response. |
| **Expected result** | **200**; `engine` is `claude-haiku-5.5` (the AI, not the fallback); `warnings` empty; both tasks have at least one block; finishes within 10 s. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | 200, engine `claude-haiku-5.5`, 5 blocks, no warnings, **3,184 ms**. |
| **Status** | **Pass** |

#### TC-10-03: Invalid Input Is Rejected

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Invalid input. Bad task data never reaches the planner or the database. |
| **Requirement** | FR-27 |
| **Preconditions** | Student registered and signed in, no tasks. |
| **Inputs and system conditions** | `POST /api/tasks` five times with bad data; `POST /api/agent/generate` with a bad day. |
| **Test data** | (a) no title; (b) `dueAt` "next friday"; (c) `estimatedMinutes` 0; (d) `priority` 5; (e) `type` "party"; (f) generate with `{"day": "Funday"}`. |
| **Steps** | 1. Send each request. 2. List tasks. 3. Count the student's rows in Supabase `tasks`. |
| **Expected result** | Every request returns **400** with a message saying what's wrong. 0 tasks through the API and 0 rows in Supabase. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | (a) 400 "Enter a title"; (b) 400 "dueAt must be a UTC date/time like 2026-12-01T23:59:00Z"; (c) 400 "Estimated time must be between 1 minute and 7 days"; (d) 400 "Priority must be 1 (high), 2 (medium) or 3 (low)"; (e) 400 "type must be assignment, exam, study or other"; (f) 400 "day must be Mon, Tue, ... or Sun". 0 tasks, 0 rows. |
| **Status** | **Pass** |

#### TC-10-04: Request Without Preferences Is Refused

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Alternative flow. The student hasn't set study preferences, so there's nothing to plan around. |
| **Requirement** | FR-27 |
| **Preconditions** | Student registered and signed in; no preferences saved. |
| **Inputs and system conditions** | `POST /api/agent/generate`. |
| **Test data** | One task, "HIST 1301 Essay", due in 3 days. |
| **Steps** | 1. Create the task. 2. Call generate. 3. Count the student's rows in Supabase `study_blocks`. |
| **Expected result** | **422**, message tells the student to set preferences first; no schedule saved. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | 422 "Set your study preferences before generating a schedule"; 0 rows in `study_blocks`. |
| **Status** | **Pass** |

#### TC-11-02: Data Is Saved, Updated and Read Back

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Successful flow. A student's changes are stored in Supabase and come back unchanged. |
| **Requirement** | FR-28 |
| **Preconditions** | Student registered and signed in. |
| **Inputs and system conditions** | `PUT /api/settings/preferences`, `POST /api/tasks`, `PATCH /api/tasks/{id}`, `PUT /api/tasks/{id}/status`, `GET /api/tasks/{id}`, `GET /api/settings/preferences`. |
| **Test data** | New task "CS 3354 Project demo", due in 5 days, 90 min, priority 2. Changes: 150 min, priority 1, notes "practice the demo twice", status IN_PROGRESS. |
| **Steps** | 1. Save preferences. 2. Create the task. 3. Change it. 4. Set its status. 5. Read it through the API. 6. Read the row in Supabase. 7. Read preferences. |
| **Expected result** | 200, 201, 200, 200, 200. The API and the Supabase row both show 150 min, priority 1, IN_PROGRESS, the new notes and the original title. Preferences come back `saved: true` with the values sent. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | All status codes as expected. API: 150 min, priority 1, IN_PROGRESS. Supabase row: ("CS 3354 Project demo", 150, 1, "IN_PROGRESS", "practice the demo twice"). Preferences saved, 60 min sessions, 180 min/day. |
| **Status** | **Pass** |

#### TC-12-02: AI Schedule Is Validated and Saved

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Successful flow. Only an AI schedule that passes every check is stored. The test re-checks the saved schedule itself instead of trusting the server's validator. |
| **Requirement** | FR-29 |
| **Preconditions** | TC-10-02 has just generated a plan. |
| **Inputs and system conditions** | `GET /api/schedule`; Supabase `study_blocks` and `generation_runs`. |
| **Test data** | The plan from TC-10-02. |
| **Steps** | 1. Read the saved schedule. 2. Check every block: inside study hours, clear of personal time, no longer than a session, at most 180 min a day, at least 15 min between blocks, before its task's deadline, **not in the past**, and every task has a block. 3. Count `study_blocks` rows. 4. Read the latest `generation_runs` row. |
| **Expected result** | Saved schedule equals the returned plan; no rule broken; rows in Supabase equal the blocks returned; the latest run is a successful `claude-haiku-5.5` run. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | 5 blocks, all ahead of now: Fri 16:00–17:00, 17:15–18:15 and 18:30–19:30 for the report; Sat 10:00–11:00 and 11:15–12:00 for the exam, ending when personal time starts. No rule broken; 5 rows in `study_blocks`; latest run `claude-haiku-5.5`, ok. |
| **Status** | **Pass** |

#### TC-12-03: AI Failure Falls Back to a Valid Plan

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Invalid AI response. The AI call fails, and the student still gets a valid plan from the local planner. |
| **Requirement** | FR-29, NFR-15 |
| **Preconditions** | The failure server (port 8091) is running with a model name OpenRouter doesn't know. |
| **Inputs and system conditions** | `POST /api/agent/generate`, `GET /api/agent/runs`, `GET /api/schedule` on the failure server. |
| **Test data** | One task, "PHYS 2325 Lab report", due in 3 days, 120 min. |
| **Steps** | 1. Save preferences and the task. 2. Call generate. 3. Read the runs log. 4. Re-check the saved schedule as in TC-12-02. |
| **Expected result** | **200** with `engine` "local"; a warning names the AI error; every AI attempt in the log failed and each one has a matching successful local run; the saved schedule breaks no rule. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | 200, engine `local`, warning "OpenRouter returned HTTP 400". Runs log: 2 failed `no-such-model` attempts and 2 successful local runs (saving preferences also starts a background re-plan, so there were two generations). 2 blocks saved, no rule broken. 1,200 ms. |
| **Status** | **Pass** |

### Non-functional test cases

#### TC-NFR18-02: API Response Time on Supabase

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | TC-NFR18-01 measured the backend alone with in-memory fakes. This repeats it against the real server and database. |
| **Requirement** | NFR-18: at least 95% of standard backend API requests complete within 2 seconds, excluding AI generation. |
| **Acceptance criterion** | p95 of 500 requests **< 2,000 ms**, at least 95% under 2 s, no failures. |
| **Preconditions** | One student with preferences and 20 tasks. |
| **Inputs and system conditions** | 500 requests, one at a time, rotating through `GET /api/tasks`, `GET /api/tasks/{id}`, `PATCH /api/tasks/{id}`, `GET /api/settings/preferences`, `GET /api/schedule`, `GET /api/dashboard`. Each is timed from send to full response on the client. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | p50 **238.9 ms**, p95 **498.5 ms**, max 716.7 ms; 100% under 2 s; 0 failures. |
| **Status** | **Pass** |

Per endpoint (ms):

| Endpoint | p50 | p95 | Max |
| :--- | ---: | ---: | ---: |
| `GET /api/tasks` | 162.4 | 230.7 | 329.6 |
| `GET /api/tasks/{id}` | 160.0 | 249.6 | 346.6 |
| `PATCH /api/tasks/{id}` | 363.2 | 472.3 | 517.5 |
| `GET /api/settings/preferences` | 162.4 | 255.6 | 410.5 |
| `GET /api/schedule` | 284.9 | 371.2 | 505.2 |
| `GET /api/dashboard` | 447.4 | 604.2 | 716.7 |

Nearly all of this time is the round trip between the laptop and Supabase: each database query is a network call, and the server's own work measured under 1 ms in TC-NFR18-01. Endpoints that run more queries (`PATCH`, the dashboard) take longer. A server hosted in the same region as the database would be faster.

AI generation is excluded from NFR-18. For reference, TC-10-02 took 3,184 ms, and 10 back-to-back generations took 2,913–3,455 ms. All 10 were accepted from the AI, against the 10 s limit in NFR-6.

#### TC-NFR19-02: Students Can't Reach Each Other's Data

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Student B tries to read and change student A's data through every route that takes an ID. |
| **Requirement** | NFR-19: database operations prevent unauthorized access to other students' information. |
| **Acceptance criterion** | **100%** of cross-student attempts refused; A's data unchanged in Supabase. |
| **Preconditions** | Students A and B registered. A has a course "CS 3354" and a task "A's private essay" in it. |
| **Inputs and system conditions** | As B: `GET`, `PATCH`, `PUT …/status` and `DELETE` on A's task; `PATCH` A's course; `POST` a task under A's course. With no token; with a made-up token; with A's token after logging out. |
| **Steps** | 1. Send each request. 2. List B's tasks. 3. Compare A's task row in Supabase before and after. 4. Count B's rows. |
| **Expected result** | 404 for A's task and course (the API doesn't reveal that they exist), 400 for the foreign course, 401 for the three bad tokens. B's list is empty, A's row is unchanged and B has no rows. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | GET/PATCH/status/DELETE of A's task: 404, 404, 404, 404. PATCH A's course: 404. Task under A's course: 400. No token, made-up token, logged-out token: 401, 401, 401. B's list empty; A's row unchanged; B has 0 rows. |
| **Status** | **Pass** |

### Defects found during testing (fixed)

| ID | Found by | Problem | Fix | Verified by |
| :--- | :--- | :--- | :--- | :--- |
| DEF-AA-01 | TC-12-02, TC-12-03 | Plans always covered the whole Monday–Sunday week, so on Friday most study blocks were on days that had already passed. Neither the AI prompt nor the validator knew the current time, and the local planner had the same problem. | `remainingStudyHours()` (`agent/Planning.cpp`) removes study hours before now and starts a range that's in progress at the next 5-minute mark. The AI, the validator and the local planner all use these hours. When no hours are left this week, generate returns 422 "None of your study hours are left this week." | Unit tests `Planning.RemainingStudyHoursDropThePast`, `AgentTest.NeverSchedulesStudyTimeThatHasPassed`, `AgentTest.SaysSoWhenNoStudyHoursAreLeftThisWeek`; all 8 live tests pass |
| DEF-AA-02 | Retest of TC-12-02 | With only Fri–Sun left, Claude Haiku broke the rules in **4 of 5** generations: blocks with no gap between them, blocks that ended before they started, and 240 min on a day limited to 180. Those plans fell back to the local planner. A stricter prompt made it worse: 10 of 10 rejected. | The server now works out the valid study slots itself (`GeminiClient::studySlots`): one session each, spaced by the break, clear of personal time and within the daily maximum. The AI picks slots and assigns tasks to them instead of computing times. | Unit test `Gemini.SlotsAreSessionsSpacedByBreaksAroundPersonalTime`; 10 of 10 generations accepted from the AI (2.9–3.5 s each); all 8 live tests pass |

### Unresolved defect

- **DEF-AA-03:** when the AI call fails, the first warning reads "AI or schedule storage is currently unavailable; saved plan was not intentionally replaced", even though a valid fallback plan was saved. The second warning gives the real cause. The schedule itself is correct; only the wording is misleading.

---

## Assignment 4: Unit Tests (in-memory fakes)

**What these tests cover.** These tests check the Assignment 4 design classes (`SchedulingRequestController`, `StudentDataController`, `ScheduleGenerationService`). The running server doesn't use those classes; the Assignment 5 tests above check the live routes instead.

**How these tests run.** Every Assignment 4 test is an automated GoogleTest case in `backend/tests/`. The Task Management System, Schedule System, Supabase and Gemini are replaced by in-memory fakes (at the top of `backend/tests/test_aa.cpp`), so the tests need no network and never touch a real student's data. Test data uses a fixed clock: **now = Mon 16 Nov 2026, 12:00 UTC**. Results below come from the run saved in [`logs/unit-test-results.txt`](logs/unit-test-results.txt).

To run them:

```bash
cmake -S backend -B backend/build
cmake --build backend/build
./backend/build/cramberry_tests --gtest_filter='AATests.*'
```

**Result summary:** 5 run, 5 Pass, 0 Fail, 0 Not Run.

| Test case | Type | Requirement | Status |
| :--- | :--- | :--- | :--- |
| TC-10-01 Valid Request Is Accepted | Functional | FR-27 | Pass |
| TC-11-01 Save and Update Own Records | Functional | FR-28 | Pass |
| TC-12-01 Schedule That Breaks the Rules Is Rejected | Functional | FR-29 | Pass |
| TC-NFR18-01 Backend API Response Time | Non-functional | NFR-18 | Pass |
| TC-NFR19-01 Students Can't Reach Each Other's Data | Non-functional | NFR-19 | Pass |

---

## UC-10 Process Scheduling Request

> **Purpose:** Receives and validates the student's tasks, deadlines and preferences before the scheduling request is processed.
> **Flow (sequence diagram):** POST /api/schedule → validate(fields, dates, types) → *[valid]* getTaskDetails() → prepareRequest() → forwardRequest() → 202 Accepted; *[invalid]* 400 Bad Request (fieldErrors)
> **Code:** `backend/api/SchedulingRequestController.{h,cpp}` · **Test:** `backend/tests/test_aa.cpp`

Stored tasks used by the UC-10 test:

| Task ID | Owner | Title | Due (UTC) | Est. | Completed |
| :--- | :--- | :--- | :--- | :--- | :--- |
| t-essay | student-a | HIST 1301 Essay | 2026-11-20 23:59 | 240 min | No |
| t-quiz | student-a | CS 3354 Quiz 4 | 2026-11-18 09:00 | 90 min | No |

### TC-10-01: Valid Request Is Accepted

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Successful flow. A valid request is accepted and forwarded to the Schedule System. |
| **Requirement** | FR-27 |
| **Preconditions** | student-a is signed in. The two stored tasks above exist. The Schedule System is available. |
| **Inputs and system conditions** | `POST /api/schedule` as student-a. |
| **Test data** | tasks = [{t-essay, "2026-11-20T23:59", 240}, {t-quiz, "2026-11-18T09:00", 120}]; study hours Mon–Fri 16:00–21:00; 60 min sessions; 15 min breaks; 180 min/day max. |
| **Steps** | 1. Submit the request. 2. Check the response. 3. Check what was forwarded to the Schedule System. |
| **Expected result** | **202 Accepted**, message **"Building your schedule"**, request ID `req-1`, no field errors. Exactly one request is forwarded. It has student-a's ID, tasks ordered earliest deadline first (t-quiz, then t-essay), and t-quiz's estimate updated to 120 min. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | 202, "Building your schedule", req-1. One request forwarded with tasks [t-quiz (120 min), t-essay]. |
| **Status** | **Pass** |

---

## UC-11 Save/Retrieve Student Data

> **Purpose:** Lets students save, retrieve and update their assignments, schedules and preferences through the backend.
> **Flow (sequence diagram):** GET or PATCH /api/data → checkAccess(studentId, recordId) → *[allowed]* selectRecords / updateRecords → 200 OK; *[denied]* 403 "You can't open this record"
> **Code:** `backend/api/StudentDataController.{h,cpp}`, `backend/data/StudentDataRepository.h` · **Test:** `backend/tests/test_aa.cpp`

Stored records used by the UC-11 test:

| Record | Owner | Type | Fields |
| :--- | :--- | :--- | :--- |
| rec-a1 | student-a | Assignment | title "CS 3354 Project Phase 4", due 2026-11-20T23:59, 240 min, In Progress |
| rec-a2 | student-a | Assignment | title "MATH 2418 HW 6", due 2026-11-17T23:59, 60 min, Not Started |
| rec-b1 | student-b | Assignment | title "PHYS 2325 Lab Report", due 2026-11-19T23:59, 120 min, Not Started |

### TC-11-01: Save and Update Own Records

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Successful flow (update and create). Changes are saved and still there after a reload. |
| **Requirement** | FR-28 |
| **Preconditions** | student-a is signed in. Stored records as above. |
| **Inputs and system conditions** | `PATCH /api/data/rec-a1`, `GET /api/data/rec-a1`, `POST /api/data`. |
| **Test data** | Changes {status: "Completed", estimatedMinutes: "180"}; new assignment {title: "CS 3354 Quiz 5", dueDate: "2026-11-23T09:00"}. |
| **Steps** | 1. Update rec-a1. 2. Read rec-a1 again. 3. Create the new assignment. 4. List assignments. |
| **Expected result** | Step 1: **200 "Saved."**. Step 2: status Completed, 180 min, title unchanged. Step 3: **201 "Saved."**, owner student-a. Step 4: 3 assignments. Exactly 2 writes, and student-b's record is unchanged. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | As expected: 200 "Saved.", values kept after reload, 201 "Saved.", 3 assignments, 2 writes, rec-b1 unchanged. |
| **Status** | **Pass** |

---

## UC-12 Validate AI Response

> **Purpose:** Validates AI-generated schedule data, handles invalid responses and stores valid schedules in the database.
> **Flow (sequence diagram):** generateSchedule() → sendPrompt() → proposedSchedule (JSON) → parseResponse() → checkFieldsAndFormat() → *[valid]* saveSchedule() → *[saved]* newSchedule / *[save fails]* error, existing schedule kept; *[invalid]* error, AI response rejected
> **Code:** `backend/agent/ScheduleGenerationService.{h,cpp}` · **Test:** `backend/tests/test_aa.cpp`

The UC-12 test uses this request: student-a, submitted at now, tasks **t-quiz** "CS 3354 Quiz 4" (due 2026-11-18 09:00, 120 min) and **t-essay** "HIST 1301 Essay" (due 2026-11-20 23:59, 240 min). student-a's existing saved schedule is one block, "Last week's plan", 2026-11-15 18:00–19:00.


### TC-12-01: Schedule That Breaks the Rules Is Rejected

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Invalid input. The JSON is well formed, but the schedule itself is wrong. |
| **Requirement** | FR-29 |
| **Preconditions** | Gemini returns a six-event reply with logic errors. |
| **Inputs and system conditions** | generateSchedule(request). |
| **Test data** | Event 1 t-quiz 18 Nov 08:00–10:00 (ends after the 09:00 deadline); Event 2 t-quiz 16 Nov 18:00–19:00; Event 3 t-essay 16 Nov 18:30–19:30 (overlaps event 2); Event 4 t-lab (unknown task); Event 5 t-essay 17 Nov 21:00–20:00 (ends before it starts); Event 6 t-essay 15 Nov 10:00–11:00 (in the past). |
| **Steps** | 1. Generate the schedule. 2. Check the reasons. |
| **Expected result** | **Rejected** with reasons: "Event 1 ends after \"CS 3354 Quiz 4\" is due", "Event 4 refers to unknown assignment \"t-lab\"", "Event 5 ends before it starts", "Event 6 starts in the past", "Event 3 overlaps event 2". Existing schedule unchanged; 0 saves. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | Rejected with exactly those five reasons. Existing schedule unchanged, 0 saves. |
| **Status** | **Pass** |

---

## Non-Functional Test Cases

### TC-NFR18-01: Backend API Response Time

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Measure how long standard backend requests take. |
| **Requirement** | NFR-18: at least 95% of standard backend API requests complete within 2 seconds, excluding AI generation. |
| **Acceptance criterion** | 95th percentile (p95) of 1,000 requests **< 2,000 ms**. |
| **Preconditions** | 40 stored tasks and 40 stored assignment records for student-a. |
| **Inputs and system conditions** | 1,000 requests, rotating through: UC-10 process a 40-task request; UC-11 list assignments; UC-11 update a record; UC-12 validate a 40-event AI reply. Each request is timed separately with a monotonic clock. |
| **Test data** | Tasks t-0…t-39, all due 2026-11-20 23:59; 40 non-overlapping 45 min events between 17 and 20 Nov. |
| **Steps** | 1. Run the 1,000 requests. 2. Sort the times. 3. Read the 95th percentile. |
| **Expected result** | p95 < 2,000 ms, and every request succeeds. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | p95 = **0.68 ms**, max = 0.95 ms over 1,000 requests (MacBook, Apple clang, debug build). |
| **Status** | **Pass**, with a limit: the in-memory fakes stand in for Supabase, so this measures the backend's own processing, not network or database time. TC-NFR18-02 repeats it against the real server and Supabase. |

### TC-NFR19-01: Students Can't Reach Each Other's Data

| Field | Value |
| :--- | :--- |
| **Purpose / scenario** | Every student tries to read and change every other student's records. |
| **Requirement** | NFR-19: database operations prevent unauthorized access to other students' information. |
| **Acceptance criterion** | **100%** of cross-student reads and writes are refused (403), and no data changes. |
| **Preconditions** | 3 students, 3 assignment records each. |
| **Inputs and system conditions** | GET and PATCH on every other student's record; PATCH trying to set `ownerId` to another student; list as each student; a UC-10 request naming another student's task, both with a correct task source and with a broken one that ignores the student ID. |
| **Test data** | student-a, student-b, student-c; records `<student>-rec-0..2`; student-b's task t-b. |
| **Steps** | 1. Run all 18 cross-student read/update pairs. 2. Try to reassign ownership. 3. List as each student. 4. Try to schedule t-b as student-a. |
| **Expected result** | 18/18 reads → 403; 18/18 updates → 403; ownership changes → 400; lists contain only the reader's records; UC-10 → 400 and nothing forwarded, even when the task source returns the other student's row. Data unchanged, 0 writes. |
| **Created by** | Ahnaf Adib (AA) |
| **Actual result** | All as expected. 0 writes. Data unchanged. |
| **Status** | **Pass** |

---

## Requirements Traceability Matrix (AA rows)

### Functional RTM

| Requirement ID | Requirement | Use case ID | Test case IDs |
| :--- | :--- | :--- | :--- |
| FR-27 | Backend API routes receive and validate student tasks, deadlines and scheduling preferences. | UC-10 | TC-10-01, TC-10-02, TC-10-03, TC-10-04 |
| FR-28 | Store, retrieve and update student schedules, assignments and preferences using Supabase. | UC-11 | TC-11-01, TC-11-02 |
| FR-29 | Validate AI-generated responses, handle invalid outputs and save processed schedules to the database. | UC-12 | TC-12-01, TC-12-02, TC-12-03 |

### Non-Functional RTM

| Requirement ID | Requirement | Feature / system scope | Related use cases | Test case IDs |
| :--- | :--- | :--- | :--- | :--- |
| NFR-18 | ≥ 95% of standard backend API requests complete within 2 s, excluding AI generation. | Backend API System (all routes) | UC-10, UC-11, UC-12 | TC-NFR18-01, TC-NFR18-02 |
| NFR-19 | Database operations prevent access to other students' data and keep data consistent if an operation fails. | Database layer / Backend API System | UC-10, UC-11 | TC-NFR19-01, TC-NFR19-02 |

---

## Known Limitations

1. `SchedulingRequestController`, `StudentDataController` and `ScheduleGenerationService` (the Assignment 4 design classes) are not called by the running server. The live API covers UC-10, UC-11 and UC-12 through `/api/tasks`, `/api/settings/preferences` and `/api/agent/generate`.
2. TC-NFR18-02 was measured from a laptop to the hosted Supabase database, so most of each response time is network round trip.
3. DEF-AA-03 above.
