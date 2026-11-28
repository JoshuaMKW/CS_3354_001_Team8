# JA live test results: UC-15 and UC-16

| Item | Value |
| :--- | :--- |
| Date | Fri 27 Nov 2026, 23:42 CST |
| Tester | Ahnaf Adib (AA) |
| Test author | Joshua Alston (JA) |
| Machine | Darwin 27.0.0 (arm64) |
| Server | `cramberry_server` from `backend/out/build`, port 8101; frontend Vite dev server on port 3101 |
| Database | Supabase hosted PostgreSQL (db.<project>.supabase.co:5432) |
| AI | `anthropic/claude-haiku-5.5` through OpenRouter |
| Client | Python 3 `urllib`, one request at a time |
| Browser | Chromium through Playwright for Python 1.49.1, time zone America/Chicago |

**6 run, 6 Pass, 0 Fail.**

| Test case ID | Requirement / use case | Tester | Inputs | Expected result | Actual result | Pass/Fail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TC-15-01 Save valid study preferences | UC-15, FR-7, NFR-2 | Ahnaf Adib (AA) | Study Planner settings page. Painted Mon, Wed and Fri 4 pm to 7 pm with the Study hours tool, Most study per day 3 hours, Session length 60 min, Break 15 min, added personal time Sun 6 pm to 7 pm with Add, clicked Save settings, reloaded, then read the study_preferences row. | A save confirmation appears. After the reload every value is still shown. The study_preferences row holds the same values and no other student's row has changed. | "Saved. Cramberry is rebuilding your schedule." appeared. After the reload the page showed "9 study hours marked this week", Mon, Wed, Fri 4 to 7 pm as study hours, max 180 min, session 60 min, break 15 min, personal time ['Personal time, Sun, 6 to 7 pm']. The row holds study hours [('Fri', '16:00', '19:00'), ('Mon', '16:00', '19:00'), ('Wed', '16:00', '19:00')], max 180, session 60, break 15, personal time [('Sun', '18:00', '19:00')]. Other accounts' rows unchanged: True. | **Pass** |
| TC-15-02 Personal time conflict | UC-15 CheckForConflicts() exception, Show Conflict | Ahnaf Adib (AA) | Accepted schedule with one block "CS 3354 Review" on Saturday 28 Nov 6:00 pm to 7:30 pm (made by generating a plan for a 90 min task). On Study Planner settings, added personal time Sat 6:00 pm to 8:00 pm with Add, clicked Save settings, then opened View Schedule. | A conflict message names "CS 3354 Review" on Sat from 6:00 to 7:30 PM. GenerateSchedule() is not called. On View Schedule the block is still there and unchanged. | The page said "Nothing was saved" and listed ['CS 3354 Review, Sat, 6 to 7:30 pm']. generation_runs rows 3 before and 3 after. View Schedule showed ['CS 3354 Review, Sat 6 to 7:30 pm. Open in the editor']. study_blocks [('CS 3354 Review', 5, 1080, 1170)] before, [('CS 3354 Review', 5, 1080, 1170)] after. | **Pass** |
| TC-15-03 Break longer than session | UC-15 validation, break not longer than a session | Ahnaf Adib (AA) | Saved preferences session 90 min, break 15 min. On Study Planner settings, typed 120 in Break after each session, tried to click Save settings, reloaded, then read the study_preferences row. | The message "Break can't be longer than your 90 min session" appears next to the field and Save settings is disabled. Nothing is written to the database. After the reload the break is still 15 min. | The page showed "Break can't be longer than your 90 min session." under the break field and Save settings disabled=True. Save requests sent: 0. Row breakMinutes 15, updated_at unchanged: True. After the reload the break showed "15 min". | **Pass** |
| TC-15-04 Schedule follows preferences | UC-15 and UC-5, FR-16 | Ahnaf Adib (AA) | Saved preferences: study hours Saturday 28 Nov, Sunday 29 Nov 4:00 to 7:00 pm, most study per day 2 hours, session 60 min, break 15 min, personal time Sun 5:00 to 6:00 pm. Three 60 min assignments due Saturday 28 Nov 11:59 pm, Sunday 29 Nov 11:59 pm, Sunday 29 Nov 11:59 pm. Clicked Generate my schedule on Create Schedule, then read the plan on View Schedule. | Every block falls on Sat, Sun between 4:00 PM and 7:00 PM. No block overlaps Sun 5:00 to 6:00 PM. No day has more than 2 hours, no block is longer than 60 min, at least 15 min between back-to-back blocks. All three assignments are scheduled before their due dates. | Planned by the built-in planner. 3 blocks: JA assignment 1 Sat 16:00-17:00; JA assignment 2 Sat 17:15-18:15; JA assignment 3 Sun 16:00-17:00. Rule problems: none. | **Pass** |
| TC-16-01 Opt in to email reminders | UC-16, FR-12 | Ahnaf Adib (AA) | Notifications page, notifications off. Clicked On, turned on Email and Deadline Reminders, chose 1 day before, clicked Save settings, reloaded, then read the notification_settings row. | A confirmation message appears. After the reload Enable Notifications, Email and Deadline Reminders (1 Day) are still on. The database row matches what was saved. | "Notification settings saved." appeared. After the reload enabled=True, email=True, desktop=False, sms=False, deadline reminders=True at 1440 min. Row (enabled, email, desktop, sms, phone, reminders, lead) = (1, 1, 0, 0, '', 1, 1440). | **Pass** |
| TC-16-03 Invalid phone number | UC-16 ValidatePreferences() exception, FR-12 | Ahnaf Adib (AA) | Notifications on with Email as the only source, no phone number. Turned on SMS, typed 12345 as the phone number, clicked Save settings, reloaded, then read the notification_settings row. | An error such as "Enter a valid phone number" appears and nothing is saved. After the reload SMS is off, no phone number is stored and Email is still the only source. | The page showed "Enter a valid phone number" under the phone field. Row unchanged: True. After the reload sms=False, phone="", email=True, desktop=False. Row (enabled, email, desktop, sms, phone) = (1, 1, 0, 0, ''). | **Pass** |

## Checks

**TC-15-01** (Pass)
- ok: save confirmation shown
- ok: after reload Mon, Wed and Fri 4 to 7 pm are study hours
- ok: after reload no other hour is marked as study (9 study hours in total)
- ok: after reload Most study per day is 3 hours, Session length 60 min, Break 15 min
- ok: after reload Sun 6 to 7 pm is personal time
- ok: study_preferences row has study hours Mon, Wed, Fri 16:00-19:00
- ok: study_preferences row has max 180, session 60, break 15
- ok: study_preferences row has personal time Sun 18:00-19:00
- ok: no other student's row changed (a second test student's saved row and every non-test account's row)

**TC-15-02** (Pass)
- ok: conflict message names "CS 3354 Review, Sat, 6 to 7:30 pm"
- ok: GenerateSchedule() not called (no new generation_runs row)
- ok: View Schedule still shows the one CS 3354 Review block at the same time
- ok: study_blocks row unchanged

**TC-15-03** (Pass)
- ok: message "Break can't be longer than your 90 min session" next to the field
- ok: Save settings is disabled
- ok: nothing written to the database (no save request, row unchanged)
- ok: after the reload the break is still 15 min

**TC-15-04** (Pass)
- ok: plan generated and drawn on View Schedule
- ok: every block is on a study day (Sat, Sun) between 4:00 and 7:00 pm
- ok: no block overlaps Sun 5:00 to 6:00 pm
- ok: no day has more than 2 hours of study
- ok: no block is longer than 60 min
- ok: at least 15 min between back-to-back blocks
- ok: all three assignments are scheduled before their due dates

**TC-16-01** (Pass)
- ok: confirmation message appears
- ok: after reload Enable Notifications, Email and Deadline Reminders (1 day) are on
- ok: database row matches what was saved (on, email only, reminders 1440 min)

**TC-16-03** (Pass)
- ok: error "Enter a valid phone number" shown
- ok: not saved (row unchanged)
- ok: after reload SMS is off and no phone number is stored
- ok: after reload Email is still the only source
