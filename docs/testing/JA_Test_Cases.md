# Software Testing — Functional Test Cases (JA)

**Project:** Cramberry (Group 8)
**Test author:** Joshua Alston (JA)
**Use cases covered:** UC-15 Manage Study Preferences, UC-16 Update Notification Settings

Each test case uses the format from Chapter 13 (Test case ID, Test author, Requirement, Preconditions, Test steps, Expected result, Actual result, Pass/Fail). They are **black-box** tests: each one comes from a requirement and the expected behavior in the use case and sequence diagrams, not from the code. *Actual result* and *Pass/Fail* stay blank until the test is run.

Every test runs against the stack in the SOW/WOW: the Next.js app on a Vercel preview deployment, Supabase Auth and PostgreSQL, and the Gemini API. Each test uses a dedicated **test student account**, never a real student's account.

---

## UC-15 — Manage Study Preferences

> **Purpose:** Lets the student define personal study habits and constraints, such as study hours, breaks and personal time.
> **Components:** User Profile System, Database System, AI Scheduling System
> **Flow (sequence diagram):** Update Study Schedule → GetActiveStudyItems() → CheckForConflicts() → *[no conflicts]* GenerateSchedule() → Transact → Confirmation, *[exception]* Show Conflict

### TC-JA-01 — Save valid study preferences

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-01 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-15 Manage Study Preferences |
| **Test level** | System (end to end) |
| **Requirement** | FR-7: The system shall let students add constraints to the schedule maker. NFR-2: Students shall be able to revise schedule requirements conveniently. |
| **Preconditions** | The test student is signed in and has default study preferences. The student has no accepted schedule yet. The student is on *Study Planner → Set Study Settings*. |
| **Test steps** | 1. Mark Mon, Wed and Fri from 4:00 PM to 7:00 PM as study hours. <br>2. Set *Most study per day* to 3 hours and *Session length* to 60 min. <br>3. Set *Break after each session* to 15 min. <br>4. Add personal time on Sun from 6:00 PM to 7:00 PM. <br>5. Click **Save settings**. <br>6. Reload the page. <br>7. Check the student's `PlannerSettings` row in Supabase. |
| **Expected result** | A save confirmation appears. After the reload, every value from steps 1–4 is still shown. The `PlannerSettings` row for this student holds the same values, and no other student's row has changed. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

### TC-JA-02 — Reject a break longer than the session length

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-02 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-15 Manage Study Preferences (Validate Data → *extends* Show Error) |
| **Test level** | System |
| **Requirement** | UC-15 validation rule: The system shall validate study preferences before saving them and shall reject a break that is longer than one study session. |
| **Preconditions** | The test student is signed in. Saved preferences: *Session length* = 90 min, *Break after each session* = 15 min. The student is on *Set Study Settings*. |
| **Test steps** | 1. Change *Break after each session* to 120 min. <br>2. Try to click **Save settings**. <br>3. Reload the page. <br>4. Check the student's `PlannerSettings` row in Supabase. |
| **Expected result** | The message "Break can't be longer than your 90 min session" appears next to the field, and **Save settings** is disabled. Nothing is written to the database. After the reload, the break is still 15 min. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

### TC-JA-03 — Personal time that conflicts with an existing study block

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-03 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-15 Manage Study Preferences (sequence diagram: CheckForConflicts() → *[exception]* Show Conflict) |
| **Test level** | Integration (User Profile System + Database System) |
| **Requirement** | UC-15: When new preferences conflict with study items already scheduled, the system shall show the conflict instead of silently overwriting the schedule. |
| **Preconditions** | The test student is signed in and has an accepted schedule. That schedule contains one study block, "CS 3354 Review", on Tue from 6:00 PM to 7:30 PM. |
| **Test steps** | 1. Open *Set Study Settings*. <br>2. Add personal time on Tue from 6:00 PM to 8:00 PM. <br>3. Click **Save settings**. <br>4. Open *Schedule → View Schedule*. |
| **Expected result** | The system shows a conflict message that names "CS 3354 Review" on Tue from 6:00 to 7:30 PM. It does not call GenerateSchedule(). On *View Schedule*, the "CS 3354 Review" block is still there and unchanged. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

### TC-JA-04 — Generated schedule follows the saved preferences

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-04 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-15 Manage Study Preferences → UC-5 Generate AI Study Schedule (*include*) |
| **Test level** | Integration (User Profile System + AI Scheduling System via Gemini) |
| **Requirement** | FR-16: The AI shall generate daily and weekly study schedules based on the student's tasks, deadlines and availability. UC-15: The AI Scheduling System shall follow the student's saved study preferences. |
| **Preconditions** | The test student is signed in. Saved preferences: study hours are Mon–Fri from 4:00 PM to 7:00 PM, *Most study per day* = 2 hours, *Session length* = 60 min, *Break* = 15 min, and personal time is Wed from 5:00 PM to 6:00 PM. The student has three incomplete assignments due within the next 7 days. |
| **Test steps** | 1. Open *Study Planner* and click **Generate New Plan**. <br>2. Review the generated draft schedule. <br>3. Check each study block against the saved preferences. |
| **Expected result** | Every study block falls on Mon–Fri between 4:00 PM and 7:00 PM. No block overlaps Wed from 5:00 PM to 6:00 PM. No day has more than 2 hours of study, and no block is longer than 60 min. There are at least 15 min between back-to-back blocks. All three assignments are scheduled before their due dates. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

---

## UC-16 — Update Notification Settings

> **Purpose:** Sends timed alerts by email, push or calendar to remind the student of upcoming study blocks, assignment deadlines and exams.
> **Components:** Notification System, Database System
> **Flow (sequence diagram):** Update Notifications → ValidatePreferences() → *[validated]* Transact(acc) → ok → Confirmation, *[exception]* Show Error

### TC-JA-05 — Opt in to email deadline reminders

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-05 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-16 Update Notification Settings (*[validated]* path) |
| **Test level** | System |
| **Requirement** | FR-12: The system shall provide an opt-in email and/or phone number notification system. |
| **Preconditions** | The test student is signed in with a verified test email address. Notifications are off. The student is on *Account → Notifications*. |
| **Test steps** | 1. Turn on **Enable Notifications**. <br>2. Under *Notification Sources*, turn on **Email**. <br>3. Turn on **Deadline Reminders** and set it to 1 Day. <br>4. Click **Save**. <br>5. Reload the page. <br>6. Check the student's notification settings row in Supabase. |
| **Expected result** | A confirmation message appears. After the reload, Enable Notifications, Email and Deadline Reminders (1 Day) are still on. The database row matches what was saved. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

### TC-JA-06 — Deadline reminder is sent at the chosen time

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-06 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-16 Update Notification Settings → Trigger Notification Reminders |
| **Test level** | Integration (Notification System + Task Management System + Notification Service) |
| **Requirement** | UC-16: The system shall automatically send timed alerts that tell the student about upcoming assignment deadlines. |
| **Preconditions** | The settings from TC-JA-05 are saved: Email is on and Deadline Reminders is set to 1 Day. The test inbox is empty. |
| **Test steps** | 1. Create an assignment "Test HW 1" that is due 24 hours and 10 minutes from now. <br>2. Wait 15 minutes. <br>3. Check the test inbox. <br>4. Wait 15 more minutes and check the inbox again. |
| **Expected result** | Exactly **one** reminder email arrives about 24 hours before the due date (within ±5 min). It names "Test HW 1" and shows its due date and time. No duplicate email arrives in step 4. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

### TC-JA-07 — Reject an invalid phone number for SMS reminders

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-07 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-16 Update Notification Settings (ValidatePreferences() → *[exception]* Show Error) |
| **Test level** | System |
| **Requirement** | FR-12: The system shall provide an opt-in email and/or phone number notification system. UC-16: The system shall validate notification preferences before saving them. |
| **Preconditions** | The test student is signed in and is on *Account → Notifications*. Notifications are on with Email as the only source. No phone number is on file. |
| **Test steps** | 1. Turn on the phone/SMS notification source. <br>2. Enter `12345` as the phone number. <br>3. Click **Save**. <br>4. Reload the page and check the notification settings row in Supabase. |
| **Expected result** | The system shows an error such as "Enter a valid phone number" and does not save. After the reload, SMS is still off, no phone number is stored, and Email is still the only source. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

### TC-JA-08 — Opt out of all notifications

| Field | Value |
| :--- | :--- |
| **Test case ID** | TC-JA-08 |
| **Test author** | Joshua Alston (JA) |
| **Use case** | UC-16 Update Notification Settings |
| **Test level** | System |
| **Requirement** | FR-13: The system shall let the student remove themselves from the notification system. |
| **Preconditions** | The test student is signed in. Notifications are on with Email and Desktop Notification as sources, and Deadline Reminders is set to 1 Day. An assignment "Test HW 2" is due 24 hours and 10 minutes from now. The test inbox is empty. |
| **Test steps** | 1. Open *Account → Notifications*. <br>2. Turn off **Enable Notifications**. <br>3. Click **Save**. <br>4. Wait until 30 minutes past the time the "Test HW 2" reminder would have been sent. <br>5. Check the test inbox and the browser's desktop notifications. |
| **Expected result** | A confirmation message appears and the settings show notifications as off. No email or desktop notification about "Test HW 2" is sent. |
| **Actual result** | *Leave blank until the test is run.* |
| **Pass/Fail** | *Leave blank until the actual result is compared with the expected result.* |

---

## Traceability Summary

| Test case | Use case | Requirement(s) | Path tested |
| :--- | :--- | :--- | :--- |
| TC-JA-01 | UC-15 | FR-7, NFR-2 | Valid save (main path) |
| TC-JA-02 | UC-15 | UC-15 validation | Invalid input → Show Error |
| TC-JA-03 | UC-15 | UC-15 conflict check | Conflict → Show Conflict |
| TC-JA-04 | UC-15 → UC-5 | FR-16, UC-15 | Preferences followed by AI schedule |
| TC-JA-05 | UC-16 | FR-12 | Valid opt-in (main path) |
| TC-JA-06 | UC-16 | UC-16 timed alerts | Reminder delivered on time, once |
| TC-JA-07 | UC-16 | FR-12, UC-16 validation | Invalid input → Show Error |
| TC-JA-08 | UC-16 | FR-13 | Opt-out stops all reminders |
