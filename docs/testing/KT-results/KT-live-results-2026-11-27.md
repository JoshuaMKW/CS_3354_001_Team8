# KT live test results: UC-8 and UC-9

| Item | Value |
| :--- | :--- |
| Date | Fri 27 Nov 2026, 23:28 CST |
| Tester | Ahnaf Adib (AA) |
| Test author | Kevin Tran (KT) |
| Machine | Darwin 27.0.0 (arm64) |
| Server | `cramberry_server` from `backend/out/build`, port 8103, plus port 8113 for TC-08-04 with its database connection through a local relay that the test cuts. Both with `CRAMBERRY_AUTO_REPLAN=0`, students on UTC offset 0 |
| Database | Supabase hosted PostgreSQL (db.<project>.supabase.co:5432) |
| AI | `anthropic/claude-haiku-5.5` through OpenRouter |
| Client | Python 3 `urllib`, one request at a time |

**10 run, 7 Pass, 3 Fail.**

| Test case ID | Requirement / use case | Tester | Inputs | Expected result | Actual result | Pass/Fail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TC-08-01 Exports a populated schedule | UC-8 Export Schedule. Add function to download or export schedule as a file | Ahnaf Adib (AA) | Owner with study hours Sat 28 Nov 18:00 to 19:00 UTC, task "Essay" due 2026-12-01T05:28:00Z, 60 min. Schedule generated, then GET /api/schedule/export.ics, the call the Schedule page's export button makes. | Export succeeds. The file has BEGIN:VCALENDAR, a DTSTART at the block's start time in UTC, SUMMARY:Study: Essay, SUMMARY:Due: Essay and END:VCALENDAR. | Returned 200 as text/calendar; charset=utf-8 with 2 events. The file has DTSTART:20261128T180000Z, SUMMARY:Study: Essay, DTSTART:20261201T052800Z, SUMMARY:Due: Essay. | **Pass** |
| TC-08-02 Exports an empty schedule without failing | UC-8 Export Schedule. Add function to download or export schedule as a file | Ahnaf Adib (AA) | New owner with no tasks and no schedule. GET /api/schedule/export.ics with the owner's token. | Export succeeds. The file has BEGIN:VCALENDAR and END:VCALENDAR and no BEGIN:VEVENT. | Returned 200 with a 6 line calendar and 0 events. The owner has 0 study_blocks rows. | **Pass** |
| TC-08-03 Rejects export when logged out | UC-8 Export Schedule. Add function to download or export schedule as a file | Ahnaf Adib (AA) | Owner signs in and logs out (POST /api/auth/logout). Then GET /api/schedule/export.ics with no token, and again with the logged-out token. | The request is refused with 401 Unauthorized and the export is not run. | Logout returned 204. Export with no token returned 401 {"error":"Please sign in again"}, and with the logged-out token 401 {"error":"Please sign in again"}. | **Pass** |
| TC-08-04 Export failure returns a safe error and keeps the schedule | UC-8 Export Schedule. Add function to download or export schedule as a file | Ahnaf Adib (AA) | Owner with study hours Sat 28 Nov 16:00 to 17:00 UTC, task "Essay" due 2026-12-01T05:28:00Z, schedule generated. A second server on port 8113 reaches Supabase through a local relay. The relay is cut, then GET /api/schedule/export.ics goes to port 8113. The schedule is read back on port 8103. | The export returns 500 with no calendar and no internal error details. The saved schedule is unchanged. | Export returned 500 {"error":"Something went wrong on our side. Please try again."}, and the server log has database error line. The main server still shows 1 block, [('Essay', '8b8c4d8b-1ae5-45a6-b926-ed8248529db3', 'Sat', '16:00', '17:00')]. | **Pass** |
| TC-09-01 Shares a schedule with a registered recipient | UC-9 Share / View Schedule. Add a function to share / view schedules with others | Ahnaf Adib (AA) | Owner with a "Study session" block Sat 28 Nov 15:00 to 16:00 UTC. POST /api/schedule/shares with the recipient's email in capitals (TEMP-KT-B-1795843681@CRAMBERRY.TEST). | The share is created for the recipient, matched without regard to case. Exactly one notification is sent, to the recipient, and it contains the share ID. | Share returned 201 with recipientId equal to the recipient's ID. The recipient has 1 notification, "A study schedule was shared with you. Open /api/schedule/shared-with-me/b18f4f40-7a9f-4af8-a91a-5cb92f9a1909". | **Pass** |
| TC-09-02 Recipient views the shared schedule | UC-9 Share / View Schedule. Add a function to share / view schedules with others | Ahnaf Adib (AA) | Owner with a "Study session" block Sat 28 Nov 15:00 to 16:00 UTC shares with the recipient. Recipient calls GET on the viewEndpoint from the share response, /api/schedule/shared-with-me/{shareId}. | The recipient sees the share with the owner's ID and exactly the owner's study block. | The recipient's view returned 404 {'error': 'Shared schedule not found'}. The owner's schedule has [('Study session', '9f18b899-0742-434b-922c-8a684fd9362b', 'Sat', '15:00', '16:00')]. | **Fail** |
| TC-09-03 Shared schedule reflects the owner's edit on refresh | UC-9 Share / View Schedule. Add a function to share / view schedules with others. NFR. When sharing schedules to others it should update in real-time | Ahnaf Adib (AA) | Owner with study hours Sat 28 Nov 15:00 to 16:00 UTC and Sun 29 Nov 16:00 to 17:00 UTC, "Study session" planned on Sat only, shared with the recipient. Recipient views it. Owner moves the block to Sun 29 Nov 16:00 to 17:00 UTC with PATCH /api/schedule/blocks. Recipient views it again. | The first view shows the block at its first time. After the owner moves it, the refreshed view shows one block at the new time. | The first view returned 404 {'error': 'Shared schedule not found'}. The move returned 200, and the refreshed view returned 404 {'error': 'Shared schedule not found'}. | **Fail** |
| TC-09-04 Rejects an invalid or unregistered recipient | UC-9 Share / View Schedule. Add a function to share / view schedules with others | Ahnaf Adib (AA) | POST /api/schedule/shares as the owner with "not-an-email", then "missing@utd.edu", then the owner's own email (temp-kt-1795843681@cramberry.test). | An invalid email and the owner's own email are rejected as invalid input. An unregistered email returns not found. No notification is sent. | "not-an-email" returned 400 {'error': 'Enter a valid email address'}, "missing@utd.edu" 404 {'error': 'A registered account with that email was not found'}, and the owner's email 400 {'error': "You can't share a schedule with yourself"}. The owner has 0 share rows. | **Pass** |
| TC-09-05 Denies unauthorized and unauthenticated shared schedule access | UC-9 Share / View Schedule. Add a function to share / view schedules with others | Ahnaf Adib (AA) | Owner shares with the recipient. GET /api/schedule/shared-with-me/{shareId} as the owner, as a third student, and with no token. | The owner and an outsider get not found for the share. A request with no sign-in gets 401 and the view is not run. | The owner got 404 {'error': 'Shared schedule not found'}, the outsider 404 {'error': 'Shared schedule not found'}, and no token 401 {'error': 'Please sign in again'}. The recipient got 404 on the same link. | **Pass** |
| TC-09-06 Deleted event disappears from the shared schedule | UC-9 Share / View Schedule. Add a function to share / view schedules with others. NFR. When sharing schedules to others it should update in real-time | Ahnaf Adib (AA) | Owner with study hours Sat 28 Nov 15:00 to 16:00 UTC and Sun 29 Nov 15:00 to 16:00 UTC. Tasks "Biology Study Session" due 2026-11-28T20:00:00Z and "Physics Study Session" due 2026-11-29T23:59:00Z, schedule generated and shared. Owner deletes the Biology block with DELETE /api/schedule/blocks. Recipient views the share before and after. | The shared view shows 2 blocks. After the owner deletes the Biology block, it shows only the Physics block. | The first view returned 404 {'error': 'Shared schedule not found'}. The delete returned 200, and the refreshed view returned 404 {'error': 'Shared schedule not found'}. | **Fail** |

## Checks

**TC-08-01** (Pass)
- ok: owner's schedule has one Essay block on Sat 28 Nov 18:00 to 19:00 UTC
- ok: export returns 200 as a text/calendar attachment
- ok: file has BEGIN:VCALENDAR
- ok: file has DTSTART:20261128T180000Z
- ok: file has SUMMARY:Study: Essay
- ok: file has SUMMARY:Due: Essay
- ok: file has END:VCALENDAR

**TC-08-02** (Pass)
- ok: owner has no study blocks in Supabase
- ok: export returns 200
- ok: file has BEGIN:VCALENDAR
- ok: file has END:VCALENDAR
- ok: file has no BEGIN:VEVENT

**TC-08-03** (Pass)
- ok: logout returns 204
- ok: export with no token returns 401
- ok: export with the logged-out token returns 401
- ok: no calendar is returned

**TC-08-04** (Pass)
- ok: owner's schedule has one Essay block on Sat 28 Nov 16:00 to 17:00 UTC
- ok: second server answered health 200 before the cut
- ok: export returns 500
- ok: response has no calendar
- ok: response is the generic error with no internal details
- ok: schedule from the main server is unchanged
- ok: study_blocks rows in Supabase are unchanged

**TC-09-01** (Pass)
- ok: owner's schedule has one block on Sat 28 Nov 15:00 to 16:00 UTC
- ok: share returns 201
- ok: share's recipientId is the recipient
- ok: schedule_shares row in Supabase links owner and recipient
- ok: recipient has exactly one notification
- ok: owner has no notification
- ok: notification body has the share ID

**TC-09-02** (Fail)
- ok: owner's schedule has one block on Sat 28 Nov 15:00 to 16:00 UTC
- FAILED: recipient's view returns 200
- FAILED: view names the owner
- FAILED: view has exactly the owner's block

**TC-09-03** (Fail)
- ok: owner's schedule has one block on Sat 28 Nov 15:00 to 16:00 UTC
- FAILED: first view shows one block on Sat 28 Nov 15:00 to 16:00 UTC
- ok: owner's move returns 200
- FAILED: refreshed view returns 200
- FAILED: refreshed view shows one block on Sun 29 Nov 16:00 to 17:00 UTC

**TC-09-04** (Pass)
- ok: missing@utd.edu is not a registered account
- ok: "not-an-email" returns 400
- ok: missing@utd.edu returns 404
- ok: owner's own email returns 400
- ok: no schedule_shares row was created
- ok: no notification was sent to the owner

**TC-09-05** (Pass)
- ok: owner opening the share link returns 404
- ok: another student opening it returns 404
- ok: no token returns 401
- ok: no schedule is returned to any of them

**TC-09-06** (Fail)
- ok: owner's schedule has Biology Sat 28 Nov 15:00 to 16:00 UTC and Physics Sun 29 Nov 15:00 to 16:00 UTC
- FAILED: first view shows 2 blocks
- ok: owner's delete returns 200
- FAILED: refreshed view returns 200
- FAILED: refreshed view shows only the Physics block
