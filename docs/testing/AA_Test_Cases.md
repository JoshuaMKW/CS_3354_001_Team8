# Software Testing: Functional and Non-Functional Test Cases (AA)

**Project:** Cramberry (Group 8)
**Test author:** Ahnaf Adib (AA)
**Use cases covered:** UC-10 Process Scheduling Request, UC-11 Save/Retrieve Student Data, UC-12 Validate AI Response
**Requirements covered:** FR-27, FR-28, FR-29, NFR-18, NFR-19

IDs follow the team's `TC-<use case>-<n>` scheme. Each test case has the fields from the assignment: test case ID, purpose/scenario, preconditions, inputs and system conditions, test data, steps, expected result, creator, actual result and Pass/Fail/Not Run.

**How these tests run.** Every test is an automated GoogleTest case in `backend/tests/`. The Task Management System, Schedule System, Supabase and Gemini are replaced by in-memory fakes (at the top of `backend/tests/test_aa.cpp`), so the tests need no network and never touch a real student's data. Test data uses a fixed clock: **now = Mon 16 Nov 2026, 12:00 UTC**. Results below come from the run saved in [`AA-results/AA-mac-test-results.txt`](AA-results/AA-mac-test-results.txt).

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
| **Status** | **Pass**, with a limit: the in-memory fakes stand in for Supabase, so this measures the backend's own processing, not network or database time. The same test must be re-run against the deployed API once the Supabase adapter exists. |

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
| FR-27 | Backend API routes receive and validate student tasks, deadlines and scheduling preferences. | UC-10 | TC-10-01 |
| FR-28 | Store, retrieve and update student schedules, assignments and preferences using Supabase. | UC-11 | TC-11-01 |
| FR-29 | Validate AI-generated responses, handle invalid outputs and save processed schedules to the database. | UC-12 | TC-12-01 |

### Non-Functional RTM

| Requirement ID | Requirement | Feature / system scope | Related use cases | Test case IDs |
| :--- | :--- | :--- | :--- | :--- |
| NFR-18 | ≥ 95% of standard backend API requests complete within 2 s, excluding AI generation. | Backend API System (all routes) | UC-10, UC-11, UC-12 | TC-NFR18-01 |
| NFR-19 | Database operations prevent access to other students' data and keep data consistent if an operation fails. | Database layer / Backend API System | UC-10, UC-11 | TC-NFR19-01 |

---

## Incomplete Features and Remaining Work

All 5 tests pass, but they test the backend logic against in-memory fakes. The following parts are not built yet or not tested yet:

1. **Supabase adapters.** Production versions of `IStudentDataRepository`, `IGeneratedScheduleRepository` and `ITaskDetailsSource`, plus row-level-security policies on `owner_id` so the database enforces NFR-19 as well.
2. **Gemini client.** A real `IGeminiClient` that sends the prompt and returns the JSON reply, with a timeout.
3. **HTTP layer.** The Next.js routes (`POST /api/schedule`, `GET`/`POST`/`PATCH /api/data`) that read the Supabase Auth session and call these controllers.
4. **NFR-18 against the deployed system.** Re-run TC-NFR18-01 against the Vercel preview with a real database to include network and query time.
5. **Preference check on AI output.** UC-12 checks structure, times, deadlines and overlaps. Checking the AI's events against study hours and personal time is done by `findPreferenceViolations()` (JA, UC-15) and should be called from UC-12 once the weekly and dated schedule formats are unified.
6. **Untested error paths.** The code handles sign-in errors, a Schedule System or Gemini outage, a failed database write (the data-consistency half of NFR-19) and malformed AI replies, but the 5 selected test cases don't cover them.
