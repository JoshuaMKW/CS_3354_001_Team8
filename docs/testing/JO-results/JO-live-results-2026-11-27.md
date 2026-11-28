# JO live test results: UC-3 and UC-4

| Item | Value |
| :--- | :--- |
| Date | Fri 27 Nov 2026, 23:28 CST |
| Tester | Ahnaf Adib (AA) |
| Test author | Jonathan Lee (JO) |
| Machine | Darwin 27.0.0 (arm64) |
| Server | `cramberry_server` from `backend/out/build`, `http://127.0.0.1:8105` with `CRAMBERRY_AUTO_REPLAN=0`, and a second copy on `http://127.0.0.1:8115` whose database link TC-04-04 cuts |
| Database | Supabase hosted PostgreSQL (db.<project>.supabase.co:5432) |
| AI | `anthropic/claude-haiku-5.5` through OpenRouter |
| Client | Python 3 `urllib`, one request at a time |
| Frontend | Vite dev server on `http://localhost:3105`, proxying `/api` to port 8105 |
| Browser | Headless Chromium through Playwright for Python, time zone America/Chicago (UTC-6) |
| Temporary students left | temp-jo-0404-1795843623@cramberry.test |

**8 run, 4 Pass, 4 Fail.**

| Test case ID | Requirement / use case | Tester | Inputs | Expected result | Actual result | Pass/Fail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TC-03-01 Valid syllabus imports its deadline | UC-3 User uploads class syllabus. "Students should be able to import deadlines and assignments by uploading their course syllabus" | Ahnaf Adib (AA) | Upload Syllabus page, no course. File CS3345_Fall2026.txt with the line "Project 3 due Nov 28 by 11:59 pm", that is Sat 28 Nov 2026 23:59 local time (UTC-6). Clicked Find deadlines, then Add 1 task. | Upload succeeds. Exactly one task, Project 3 due at 23:59 on the syllabus date, is saved for the student. The saved deadline records the syllabus file it came from. | Page said "Added 1 task. Cramberry will re-plan your week.". Database has 1 task(s). Saved "Project 3" due Sat 28 Nov 2026 23:59 local time (UTC-6) with notes "From syllabus line 2: Project 3 due Nov 28 by 11:59 pm". | **Fail** |
| TC-03-02 Syllabus with no deadlines leaves existing data | UC-3 User uploads class syllabus. "Students should be able to import deadlines and assignments by uploading their course syllabus" | Ahnaf Adib (AA) | Student already has one task "Existing" due Sat 28 Nov 2026 21:00 local time (UTC-6). Upload Syllabus page, file CS3345_Fall2026.txt with three lines and no dated deadline. Clicked Find deadlines. | Upload succeeds with no error. The student's one existing task is still there and unchanged. No task is added. | Page said "No deadlines found" and showed no error. Database has 1 task(s), "Existing", unchanged. | **Pass** |
| TC-03-03 Invalid file is rejected | UC-3 User uploads class syllabus. "Students should be able to import deadlines and assignments by uploading their course syllabus" | Ahnaf Adib (AA) | Upload Syllabus page, file bad.exe with the bytes 01 02 03, set on the file input. Then Find deadlines if the page allowed it. | Upload of bad.exe is rejected. No task is saved. | Page showed no error and listed "bad.exe" as the chosen file. Find deadlines was enabled. Database has 0 task(s). | **Fail** |
| TC-03-04 Timeout during upload keeps existing data | UC-3 User uploads class syllabus. "Students should be able to import deadlines and assignments by uploading their course syllabus" | Ahnaf Adib (AA) | Student already has one task "Existing" due Sat 28 Nov 2026 21:00 local time (UTC-6). Upload Syllabus page, file CS3345_Fall2026.txt with "Project 3 due Nov 28 by 11:59 pm". The upload has no AI step, so the save request it sends (POST /api/tasks) was made to time out in the browser. Clicked Find deadlines, then Add 1 task. | Upload reports a failure. The student's one existing task is still there and unchanged. No task is added. | 1 save request timed out. Page showed "Some rows were not added. Fix them and try again.". Database has 1 task(s), "Existing", unchanged. | **Pass** |
| TC-04-01 Deadlines are shown in date order | UC-4 View Schedule. "Student views their generated schedule in the calendar system" | Ahnaf Adib (AA) | Created "Later" due Sun 29 Nov 2026 23:59 local time (UTC-6), then "Earlier" due Sat 28 Nov 2026 23:00 local time (UTC-6), 60 min each. Study hours 07:00 to 23:00 every day. Generated the plan, then opened View Schedule. Also GET /api/tasks?from=2026-11-27&to=2026-12-01. | Schedule loads. Both deadlines are listed, Earlier first, then Later. | Generate returned 200 with engine local and the page drew 2 block(s). Page listed Earlier, Later, the API listed Earlier, Later, and the page showed no error. | **Pass** |
| TC-04-02 Empty schedule shows a no-events message | UC-4 View Schedule. "Student views their generated schedule in the calendar system" | Ahnaf Adib (AA) | New student with no tasks and no schedule opened View Schedule (GET /api/schedule behind it). | Schedule loads without an error and shows "No events scheduled for this period". | GET /api/schedule returned 200 with 0 block(s). Page showed "No study blocks this week yet. Add your tasks and let Cramberry plan them." and no error. | **Fail** |
| TC-04-03 Another student's events are not shown | UC-4 View Schedule. "Student views their generated schedule in the calendar system". "Secure access to student's schedule & classes (JO)" | Ahnaf Adib (AA) | Student B created "Private" due Sat 28 Nov 2026 22:00 local time (UTC-6) and generated a plan. Student A, who has no data, then opened View Schedule. | Schedule loads for student A and lists no deadlines. Student B's private deadline is not shown. | B has 1 "Private" task and 1 block(s) in the database. A's page drew 0 block(s), listed no deadlines and did not show "Private". The API gave A 0 task(s) and 0 block(s). | **Pass** |
| TC-04-04 Database outage shows an error, not an empty calendar | UC-4 View Schedule. "Student views their generated schedule in the calendar system" | Ahnaf Adib (AA) | Student signed up on the second server (port 8115). Its database connection was then cut, and the student opened View Schedule with every /api request going to that server. | Schedule does not load. The page shows "Schedule unavailable; please retry" instead of an empty calendar. | GET /api/schedule returned 500 "Something went wrong on our side. Please try again.". Page showed "Couldn't check your sign-in: Something went wrong on our side. Please try again." with a Retry button, no calendar and no empty-week message. | **Fail** |

## Checks

**TC-03-01** (Fail)
- ok: page reports the upload succeeded with no error
- ok: exactly one task is saved for the student
- ok: saved task is Project 3 due at the syllabus date and time
- FAILED: saved task records the source file CS3345_Fall2026.txt

**TC-03-02** (Pass)
- ok: upload finishes without an error
- ok: page says no deadlines were found
- ok: the existing task is still the only task, unchanged

**TC-03-03** (Fail)
- FAILED: page rejects bad.exe with an error message
- FAILED: bad.exe is not taken as the syllabus
- ok: no task is saved

**TC-03-04** (Pass)
- ok: the save request was sent and timed out
- ok: page reports the upload failed
- ok: the existing task is still the only task, unchanged

**TC-04-01** (Pass)
- ok: schedule loads without an error
- ok: View Schedule lists both deadlines, Earlier first
- ok: the API lists both deadlines in the range, Earlier first

**TC-04-02** (Fail)
- ok: schedule loads without an error
- FAILED: page shows "No events scheduled for this period"

**TC-04-03** (Pass)
- ok: student B's deadline and study blocks exist in the database
- ok: schedule loads for student A without an error
- ok: student A's view lists no deadlines and draws no blocks
- ok: "Private" appears nowhere on student A's page
- ok: API gives student A no tasks and no blocks

**TC-04-04** (Fail)
- ok: schedule request fails instead of returning an empty schedule
- ok: page shows an error, not an empty calendar
- FAILED: page shows "Schedule unavailable; please retry"
