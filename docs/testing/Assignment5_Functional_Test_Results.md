# Assignment 5 functional test results

Every test case below ran against the running Cramberry application. The `cramberry_server` backend used the Supabase database and Claude Haiku 5.5 through OpenRouter. The page tests drove the React frontend in Chromium. Each member's results file has the full table, with the inputs, expected result, actual result and every individual check.

## Summary

| Member | Use cases | Run | Pass | Fail | Results file |
| :--- | :--- | ---: | ---: | ---: | :--- |
| Ahnaf Adib (AA) | UC-10, UC-11, UC-12 | 6 | 6 | 0 | [AA_Test_Cases.md](AA_Test_Cases.md) |
| Risha Mothukuru (RM) | UC-13, UC-14 | 6 | 6 | 0 | [RM_Assignment5.md](RM_Assignment5.md) |
| James Reid Thompson (RE) | UC-2 | 7 | 7 | 0 | [RE-results](RE-results/RE-live-results-2026-11-27.md) |
| Jonathan Lee (JO) | UC-3, UC-4 | 8 | 4 | 4 | [JO-results](JO-results/JO-live-results-2026-11-27.md) |
| Kevin Tran (KT) | UC-8, UC-9 | 10 | 7 | 3 | [KT-results](KT-results/KT-live-results-2026-11-27.md) |
| Nishanth Kandula (NK) | UC-5, UC-7 | 8 | 4 | 4 | [NK-results](NK-results/NK-live-results-2026-11-27.md) |
| Joshua Alston (JA) | UC-15, UC-16 | 6 | 6 | 0 | [JA-results](JA-results/JA-live-results-2026-11-27.md) |
| **Team** | | **51** | **40** | **11** | |

## How to run

Each script starts its own server from `backend/out/build/cramberry_server` on its own port, reads `backend/.env` for the Supabase and OpenRouter settings, and signs up a temporary student for each test that it deletes at the end. Build the backend first as `backend/README.md` describes, and run `npm install` in `frontend/`.

```bash
pip install "psycopg[binary]" playwright==1.49.1    # the JA and JO scripts need Playwright
export SKIP_REMINDER_WAITS=1                         # JA script: skips TC-16-02 and TC-16-04
TESTER="Your Name (XX)" python3 backend/tests/live/re_live_tests.py
TESTER="Your Name (XX)" python3 backend/tests/live/jo_live_tests.py
TESTER="Your Name (XX)" python3 backend/tests/live/kt_live_tests.py
TESTER="Your Name (XX)" python3 backend/tests/live/nk_live_tests.py
TESTER="Your Name (XX)" python3 backend/tests/live/ja_live_tests.py
```

`TESTER` is written into the Tester column of the results file. The results go to `docs/testing/<member>-results/`.

The planner only uses study hours still ahead in the current week, so each script puts its test data on days that are still ahead when it runs. The Inputs column in each results file lists the dates and times used.

## UC-2 User logging in, by James Reid Thompson (RE)

| Test case | Requirement | Result |
| :--- | :--- | :--- |
| TC-02-01 Signing up creates the account and signs the student in | FR-1, UC-2 | Pass |
| TC-02-02 A weak password is refused and no account is created | FR-1, UC-2 | Pass |
| TC-02-03 An email that is already registered can't be used again, whatever its case | FR-1, UC-2 | Pass |
| TC-02-04 The right email and password sign the student in | FR-1, UC-2 | Pass |
| TC-02-05 A wrong password and an unknown email get the same answer | FR-1, UC-2 | Pass |
| TC-02-06 After logging out, the session no longer works | FR-1, UC-2 | Pass |
| TC-02-07 Made-up tokens are refused, and a session expires after 7 days | FR-1, UC-2 | Pass |

TC-02-07 moves one session's `expires_at` in the Supabase `sessions` table to 2 minutes before and 1 minute after the 7-day limit, and checks that the server accepts and then refuses it.

## UC-3 Upload class syllabus and UC-4 View schedule, by Jonathan Lee (JO)

| Test case | Requirement | Result |
| :--- | :--- | :--- |
| TC-03-01 Valid syllabus imports its deadline | UC-3, syllabus import | Fail |
| TC-03-02 Syllabus with no deadlines leaves existing data | UC-3, syllabus import | Pass |
| TC-03-03 Invalid file is rejected | UC-3, syllabus import | Fail |
| TC-03-04 Timeout during upload keeps existing data | UC-3, syllabus import | Pass |
| TC-04-01 Deadlines are shown in date order | UC-4 View Schedule | Pass |
| TC-04-02 Empty schedule shows a no-events message | UC-4 View Schedule | Fail |
| TC-04-03 Another student's events are not shown | UC-4, secure access to a student's schedule | Pass |
| TC-04-04 Database outage shows an error, not an empty calendar | UC-4 View Schedule | Fail |

The upload cases run on the Upload Syllabus page in the browser, because the app reads the syllabus in the browser and saves each deadline through `POST /api/tasks`. The syllabus file is `CS3345_Fall2026.txt`. TC-04-04 cuts the second server's database connection after the student signs in.

Failures:
- **TC-03-01.** The page saves the deadline with the right title and due time, but the case also expects the task to record its source file name. The `tasks` row has no field for it. The notes say "From syllabus line 2: ..." with no file name. This is a feature the app doesn't have.
- **TC-03-03.** The page accepts `bad.exe`. It shows no error, lists the file as chosen and enables Find deadlines. The page saved no task. The Upload Syllabus page in `frontend/src` only turns away PDF and Word files and files over 1 MB. Defect DEF-JO-01.
- **TC-04-02.** The empty week loads without an error, but the page says "No study blocks this week yet. Add your tasks and let Cramberry plan them." The case expects "No events scheduled for this period", the message from the Assignment 4 `ViewSchedule` class.
- **TC-04-04.** The student sees an error and a Retry button, not an empty calendar. The case expects the message "Schedule unavailable; please retry". The page shows "Couldn't check your sign-in: Something went wrong on our side. Please try again.", because the sign-in check runs first and fails on the same outage.

## UC-8 Export schedule and UC-9 Share / view schedule, by Kevin Tran (KT)

| Test case | Requirement | Result |
| :--- | :--- | :--- |
| TC-08-01 Exports a populated schedule | UC-8, export schedule as a file | Pass |
| TC-08-02 Exports an empty schedule without failing | UC-8, export schedule as a file | Pass |
| TC-08-03 Rejects export when logged out | UC-8, export schedule as a file | Pass |
| TC-08-04 Export failure returns a safe error and keeps the schedule | UC-8, export schedule as a file | Pass |
| TC-09-01 Shares a schedule with a registered recipient | UC-9, share / view schedules | Pass |
| TC-09-02 Recipient views the shared schedule | UC-9, share / view schedules | Fail |
| TC-09-03 Shared schedule reflects the owner's edit on refresh | UC-9, sharing updates in real time | Fail |
| TC-09-04 Rejects an invalid or unregistered recipient | UC-9, share / view schedules | Pass |
| TC-09-05 Denies unauthorized and unauthenticated shared schedule access | UC-9, share / view schedules | Pass |
| TC-09-06 Deleted event disappears from the shared schedule | UC-9, sharing updates in real time | Fail |

The frontend has no sharing page, so the sharing cases call the routes in `backend/API.md`. Export calls the same route as the Schedule page's export button.

Failures:
- **TC-09-02, TC-09-03 and TC-09-06.** The recipient opens the `viewEndpoint` that the app returns when the owner creates the share, `GET /api/schedule/shared-with-me/{shareId}`, and always gets 404 "Shared schedule not found". The share exists, and `GET /api/schedule/shared-with-me` lists it. `backend/schedule/ScheduleSharingRoutes.cpp` reads `shareId` with `req->getParameter("shareId")`, which reads the query string, not the path. The same URL with `?shareId=<id>` added returns 200. `DELETE /api/schedule/shares/{shareId}` has the same problem. The C++ acceptance tests call the controller directly, so they never reach this route. Defect DEF-KT-01.
- **TC-09-05** passes, but the real recipient also gets 404 because of DEF-KT-01, so this run doesn't show that the access check tells a recipient apart from an outsider.

## UC-5 Generate AI study schedule and UC-7 Interact with AI chatbot, by Nishanth Kandula (NK)

| Test case | Requirement | Result |
| :--- | :--- | :--- |
| TC-05-01 Generate a conflict-free weekly plan | UC-5, FR-16 | Pass |
| TC-05-02 Insufficient time before deadline | UC-5, FR-16 | Fail |
| TC-05-03 Reject reversed availability hours | UC-5, FR-16 | Pass |
| TC-05-04 Sync tasks, calendar, and AI schedule | UC-5, FR-16 | Pass |
| TC-07-01 Answer a saved task question | UC-7, FR-20 | Fail |
| TC-07-02 Move a study block through chat | UC-7, FR-20 | Fail |
| TC-07-03 Reject a nonexistent task update | UC-7, FR-20 | Pass |
| TC-07-04 Create and schedule a task via chat | UC-7, FR-20 | Fail |

Each case enters a fixed class as personal time in the study preferences. The chat cases use the AI Assistant page's message box, `POST /api/agent/chat`.

Failures:
- **TC-05-02.** The case expects the app to report that 4 hours can't fit in a 1-hour window, ask for different hours or less work, and keep the saved plan. Generate returns 200 with the warning "Only 60 of 240 min for Physics Exam Prep fit before it is due", saves the partial plan and replaces the earlier one. It doesn't ask for any change. Defect DEF-NK-01.
- **TC-07-01.** The reply names CS 3354 Project but gives its deadline as "2026-11-30 at 05:59 UTC", a Monday, for a task due Sunday 11:59 PM local time. The chat sends due times to the AI in UTC without the student's time zone. Defect DEF-NK-02.
- **TC-07-02 and TC-07-04.** The assistant replies that it can only read the schedule, and nothing changes. `backend/API.md` says the assistant "can't change any data". Moving a block or creating a task through chat is a feature the app doesn't have.

## UC-15 Manage study preferences and UC-16 Update notification settings, by Joshua Alston (JA)

| Test case | Requirement | Result |
| :--- | :--- | :--- |
| TC-15-01 Save valid study preferences | UC-15, FR-7, NFR-2 | Pass |
| TC-15-02 Personal time conflict | UC-15 CheckForConflicts() exception | Pass |
| TC-15-03 Break longer than session | UC-15 validation | Pass |
| TC-15-04 Schedule follows preferences | UC-15 and UC-5, FR-16 | Pass |
| TC-16-01 Opt in to email reminders | UC-16, FR-12 | Pass |
| TC-16-03 Invalid phone number | UC-16, FR-12 | Pass |

These cases run on the Study Planner settings, Create Schedule, View Schedule and Notifications pages in Chromium, and check the `study_preferences` and `notification_settings` rows in Supabase.

## Unresolved defects

| ID | Found by | Defect | Where |
| :--- | :--- | :--- | :--- |
| DEF-KT-01 | TC-09-02, TC-09-03, TC-09-06 | A recipient can't open a shared schedule, and an owner can't revoke a share. Both routes read `shareId` from the query string instead of the path and return 404. | `backend/schedule/ScheduleSharingRoutes.cpp` |
| DEF-JO-01 | TC-03-03 | The Upload Syllabus page accepts an `.exe` file. | Upload Syllabus page in `frontend/src` |
| DEF-NK-01 | TC-05-02 | Generate saves a plan that can't fit the workload over the previous plan. It gives a warning but asks for no changes. | `POST /api/agent/generate` |
| DEF-NK-02 | TC-07-01 | The chatbot states due times in UTC instead of the student's local time. | Chat context in `backend/agent` |
