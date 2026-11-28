# Cramberry REST API Reference

The Cramberry backend (`cramberry_server`) serves a JSON REST API. By default it listens on `http://127.0.0.1:8080`. See [README.md](README.md) for building, running and configuration.

- [Conventions](#conventions)
- [Quick start](#quick-start)
- [Health](#health)
- [Accounts and sign-in](#accounts-and-sign-in)
- [Courses](#courses)
- [Tasks](#tasks)
- [Weekly report](#weekly-report)
- [Study preferences](#study-preferences)
- [AI planning and chat](#ai-planning-and-chat)
- [Schedule](#schedule)
- [Schedule sharing](#schedule-sharing)
- [Dashboard](#dashboard)
- [Notifications](#notifications)
- [Status codes](#status-codes)

---

## Conventions

### Authentication

Every endpoint except register, login and health needs a session token from register or login:

```
Authorization: Bearer <token>
```

Tokens last 7 days. A missing, invalid or expired token gets:

```http
401 Unauthorized
{"error": "Please sign in again"}
```

The server identifies the student from the token alone. A student can only ever read or change their own data. Asking for another student's course, task or notification returns `404`, as if it did not exist.

### Requests and responses

- Request bodies are JSON objects. If a body is required and missing, or isn't a JSON object, the response is `400 {"error": "Request body must be a JSON object"}`. Sending `Content-Type: application/json` is recommended but not required.
- Bodies are limited to 256 KB.
- Responses are JSON, except `GET /api/schedule/export.ics` and `204 No Content` responses.
- Every response has `Cache-Control: no-store` and `X-Content-Type-Options: nosniff`.

### Errors

Every error has the same shape. `details` only appears when there is a list of specific problems.

```json
{
  "error": "Couldn't fit your tasks into your study hours. ...",
  "details": ["\"Essay\" is not scheduled", "local: ..."]
}
```

Any unexpected server or database failure returns `500 {"error": "Something went wrong on our side. Please try again."}`. Internal details never appear in responses; they're written to the server log instead.

### Times and days

| Kind | Format | Example |
| :--- | :--- | :--- |
| Absolute time (due dates, timestamps) | ISO 8601 in **UTC** | `"2026-12-01T23:59:00Z"` |
| Day of the week | 3-letter English name | `"Mon"` … `"Sun"` |
| Time of day | 24-hour `HH:MM` (`H:MM` also accepted; `24:00` means midnight at the end of the day) | `"16:00"` |
| Weekly time range | `{day, start, end}` | `{"day":"Tue","start":"18:00","end":"19:30"}` |

How absolute times are read:
- `dueAt` accepts `YYYY-MM-DD` (meaning 23:59 that day), `YYYY-MM-DDTHH:MM`, or `YYYY-MM-DDTHH:MM:SS`. An optional `Z`, `+00:00` or fractional seconds (as produced by JavaScript's `toISOString()`) is allowed. Any other offset is rejected.
- Times are stored to the minute.

Weekly ranges (study hours, personal time, schedule blocks) are in the **student's local time**. Local time is the student's `utcOffsetMinutes`, a fixed offset with no daylight-saving rules, e.g. `-360` for US Central Standard Time. Schedule responses also include `startsAt`/`endsAt` in UTC for the current week.

### CORS

When the server is started with `CRAMBERRY_CORS_ORIGIN` (e.g. `http://localhost:3000`):
- It answers `OPTIONS` preflights with `204`.
- It adds `Access-Control-Allow-Origin`, `-Headers` (`Authorization, Content-Type`) and `-Methods` to every response.

---

## Quick start

```bash
# Register (or log in) and keep the token
curl -s -X POST http://127.0.0.1:8080/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"name":"Ada","email":"ada@example.com","password":"password123","utcOffsetMinutes":-360}'
TOKEN=...   # the "token" field from the response

# Save study preferences
curl -s -X PUT http://127.0.0.1:8080/api/settings/preferences -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"studyHours":[{"day":"Mon","start":"16:00","end":"21:00"},{"day":"Wed","start":"16:00","end":"21:00"}],
       "maxStudyMinutesPerDay":180,"sessionMinutes":60,"breakMinutes":15,"personalTime":[]}'

# Add a task, then generate this week's plan
curl -s -X POST http://127.0.0.1:8080/api/tasks -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"title":"CS 3354 Assignment","dueAt":"2026-11-26T23:59:00Z","estimatedMinutes":120,"priority":1}'
curl -s -X POST http://127.0.0.1:8080/api/agent/generate -H "Authorization: Bearer $TOKEN"
```

---

## Health

### `GET /api/health`

No sign-in needed.

**200 OK** (or **503** when the database can't be reached):

```json
{"status": "ok", "database": true, "ai": true}
```

| Field | Meaning |
| :--- | :--- |
| `status` | `"ok"` when the database answers, otherwise `"degraded"` |
| `database` | Whether the database answered |
| `ai` | Whether an AI key (OpenRouter or Gemini) is configured |

---

## Accounts and sign-in

### Student object

```json
{
  "id": "96f48a51-7204-4a99-bbe7-ac0ba7499a47",
  "name": "Ada Lovelace",
  "email": "ada@example.com",
  "utcOffsetMinutes": -360,
  "createdAt": "2026-11-23T20:29:00Z"
}
```

### `POST /api/auth/register`

Creates an account and signs the student in.

| Field | Type | Required | Rules |
| :--- | :--- | :--- | :--- |
| `name` | string | yes | 1–100 characters after trimming |
| `email` | string | yes | Valid address, at most 254 characters. Stored lower-case; must be unique |
| `password` | string | yes | 8–128 characters, with at least one letter and one digit |
| `utcOffsetMinutes` | integer | no | −720 to 840; default `0` |

**201 Created**

```json
{
  "token": "154a5339…7c017a",
  "expiresAt": "2026-11-30T20:29:00Z",
  "student": { …Student… }
}
```

**Errors:**
- `400` with the first rule broken, e.g. `"Password must contain at least one letter and one number"`
- `409` `"An account with this email already exists"`

### `POST /api/auth/login`

Body: `{"email": "...", "password": "..."}`. The email is case-insensitive.

**200 OK**: same body as register.

**Errors:** `401` `"Email or password is incorrect"`. This is the same message whether the email or the password is wrong.

### `POST /api/auth/logout`

Ends the session for the token in the `Authorization` header. The student's other sessions stay valid.

**204 No Content**

### `GET /api/users/me`

**200 OK**: the Student object.

### `PATCH /api/users/me`

Body: any of `{"name": string, "utcOffsetMinutes": integer}`. The rules are the same as for register.

**200 OK**: the updated Student object.

**Errors:** `400` for invalid values.

### `DELETE /api/users/me`

Body: `{"password": "..."}`.

Permanently deletes the account and everything that belongs to it, in one transaction: sessions, courses, tasks, preferences, notification settings, schedule, notifications and generation history. Other students' data is untouched.

**204 No Content**

**Errors:** `401` `"Password is incorrect"`.

---

## Courses

### Course object

```json
{"id": "5adeab54-…", "name": "Software Engineering", "code": "CS 3354", "term": "Fall 2026"}
```

### `GET /api/courses`

**200 OK**: an array of Course objects, sorted by code and then name.

### `POST /api/courses`

Body: `{"name"?: string, "code"?: string, "term"?: string}`.
- At least one of `name` or `code` is required.
- Length limits: name 120, code 30, term 30 characters.

**201 Created**: the Course object.

**Errors:** `400`.

### `PATCH /api/courses/{id}`

Body: any of `name`, `code`, `term`. Fields you leave out keep their current values.

**200 OK**: the updated Course object.

**Errors:** `400`, `404` `"Course not found"`.

### `DELETE /api/courses/{id}`

Deletes the course. Its tasks are kept, with `courseId` set to `null`.

**204 No Content**

**Errors:** `404` `"Course not found"`.

---

## Tasks

Tasks cover assignments, exams, study tasks and anything else with a deadline.

### Task object

```json
{
  "id": "92b7b492-…",
  "courseId": "5adeab54-…",
  "title": "CS 3354 Assignment",
  "notes": "",
  "type": "assignment",
  "dueAt": "2026-11-26T23:59:00Z",
  "estimatedMinutes": 120,
  "priority": 1,
  "status": "NOT_STARTED",
  "completedAt": null,
  "createdAt": "2026-11-23T20:29:00Z",
  "overdue": false
}
```

| Field | Notes |
| :--- | :--- |
| `courseId` | `null` when the task isn't linked to a course |
| `type` | `assignment`, `exam`, `study` or `other` |
| `priority` | `1` high, `2` medium, `3` low |
| `status` | `NOT_STARTED`, `IN_PROGRESS` or `COMPLETED` |
| `completedAt` | Set when the status becomes `COMPLETED`. Cleared if the task is reopened |
| `overdue` | Computed: not completed and `dueAt` is in the past |

### `GET /api/tasks`

Lists the student's tasks, earliest due first. All query parameters are optional and can be combined.

| Parameter | Example | Filters to |
| :--- | :--- | :--- |
| `courseId` | `?courseId=5adeab54-…` | Tasks in that course |
| `from` | `?from=2026-11-23` | Due at or after 00:00 UTC that day |
| `to` | `?to=2026-11-30` | Due **before** 00:00 UTC that day |
| `status` | `?status=IN_PROGRESS` | That status |
| `overdue` | `?overdue=true` | Overdue tasks only |

**200 OK**: an array of Task objects.

**Errors:** `400` for a malformed `from`, `to` or `status`.

### `POST /api/tasks`

| Field | Type | Required | Default | Rules |
| :--- | :--- | :--- | :--- | :--- |
| `title` | string | yes | | 1–200 characters |
| `dueAt` | string | yes | | ISO 8601 UTC (see [Times and days](#times-and-days)) |
| `type` | string | no | `assignment` | `assignment`, `exam`, `study` or `other` |
| `courseId` | string | no | none | Must be one of the student's courses |
| `estimatedMinutes` | integer | no | `60` | 1–10080 (7 days) |
| `priority` | integer | no | `2` | 1–3 |
| `status` | string | no | `NOT_STARTED` | |
| `notes` | string | no | `""` | At most 4000 characters |

**201 Created**: the Task object.

**Errors:** `400`, e.g. `"dueAt must be a UTC date/time like 2026-12-01T23:59:00Z"` or `"That course doesn't exist"`.

### `GET /api/tasks/{id}`

**200 OK**: the Task object.

**Errors:** `404` `"Task not found"`.

### `PATCH /api/tasks/{id}`

Body: any of the `POST` fields. Fields you leave out keep their current values. Send `"courseId": null` to unlink the task from its course.

**200 OK**: the updated Task object.

**Errors:** `400`, `404`.

### `PUT /api/tasks/{id}/status`

Body: `{"status": "NOT_STARTED" | "IN_PROGRESS" | "COMPLETED"}`.

**200 OK**: the updated Task object.

**Errors:** `400`, `404`.

### `DELETE /api/tasks/{id}`

**204 No Content**

**Errors:** `404`.

> **Automatic re-planning.** After a task is created or deleted, or changed in a way the planner uses (its title, due time or estimate, or whether it is completed), the server rebuilds the student's schedule in the background. Changing notes, course, priority, type, or between `NOT_STARTED` and `IN_PROGRESS` doesn't re-plan. A rebuild works the same as calling `POST /api/agent/generate`. If several changes arrive while a rebuild is already waiting or running, they share one rebuild, and a student never has two rebuilds running at once. The response carries an `X-Replan-Ticket` header; pass it to [`GET /api/agent/replans/{ticket}`](#get-apiagentreplansticket) to find out when that rebuild is done or why it was skipped. Start the server with `CRAMBERRY_AUTO_REPLAN=0` to turn this off (no header is sent then). Note that a rebuild replaces any manual block moves.

---

## Weekly report

### `GET /api/reports/weekly?start=YYYY-MM-DD`

Progress for the 7 days starting at 00:00 UTC on `start`. Without `start`, the report covers the current week starting Monday 00:00 UTC.

**200 OK**

```json
{
  "weekStart": "2026-11-23T00:00:00Z",
  "weekEnd": "2026-11-30T00:00:00Z",
  "completed": [ …Task… ],
  "overdue": [ …Task… ],
  "remaining": [ …Task… ],
  "averageCompletionMinutes": 1440
}
```

| Field | Contains |
| :--- | :--- |
| `completed` | Tasks completed during the week |
| `overdue` | Unfinished tasks due during the week whose due time has passed |
| `remaining` | Unfinished tasks due later in the week |
| `averageCompletionMinutes` | Average time from creating a task to completing it, for tasks completed this week. `null` if none were |

**Errors:** `400` for a malformed `start`.

---

## Study preferences

### Preferences object

```json
{
  "studyHours": [{"day": "Mon", "start": "16:00", "end": "21:00"}],
  "maxStudyMinutesPerDay": 180,
  "sessionMinutes": 60,
  "breakMinutes": 15,
  "personalTime": [{"day": "Sat", "start": "09:00", "end": "12:00"}]
}
```

| Field | Meaning |
| :--- | :--- |
| `studyHours` | When the student is willing to study (local time) |
| `maxStudyMinutesPerDay` | Most study per day |
| `sessionMinutes` | Longest single study block |
| `breakMinutes` | Minimum gap between blocks on the same day |
| `personalTime` | Blocked time; never scheduled |

### `GET /api/settings/preferences`

**200 OK**

```json
{"saved": true, "preferences": { …Preferences… }}
```

If the student hasn't saved preferences yet, `saved` is `false` and `preferences` holds the defaults (no study hours, 180/60/15).

### `PUT /api/settings/preferences`

Body: a Preferences object. Missing fields take the defaults above.

**Validation (400):**
- Session length must be greater than 0.
- Break can't be negative or longer than the session.
- Most study per day must be between 1 minute and 24 hours.
- Every range must end after it starts.

**200 OK**

```json
{"message": "Settings saved.", "preferences": { …Preferences… }}
```

After saving, the schedule is rebuilt in the background.

**409 Conflict**: new personal time overlaps blocks already on the schedule. Nothing is saved.

```json
{
  "error": "Personal time conflicts with \"Essay\" on Tue 6:00 PM-7:00 PM",
  "conflicts": [{"title": "Essay", "assignmentId": "…", "day": "Tue", "start": "18:00", "end": "19:00"}]
}
```

---

## AI planning and chat

### `POST /api/agent/generate`

Builds and saves the student's study plan for the current week (Monday–Sunday, local time). This can take up to about 10 seconds when the AI model is used.

Body (optional): `{"day": "Wed"}`. When `day` is given, only that day's study hours are used. The saved schedule is still **replaced** by the result.

**Which hours are used:** only the study hours still ahead this week. Earlier days are skipped, and a range that has already started begins at the next 5-minute mark, so no block is ever in the past.

**Which tasks are planned:**
- Every unfinished task due within the next 7 days.
- Tasks already overdue, or due after this week ends, are treated as due at the end of Sunday, so they get time wherever it fits.

**How the plan is made:**
1. If `OPENROUTER_API_KEY` (default model Claude Haiku 5.5) or `GEMINI_API_KEY` is set, the AI model proposes a plan. The server sends it the open study slots (one session each, spaced by the break, clear of personal time and within the daily maximum), and the AI picks slots and assigns tasks to them.
2. The proposal is checked against the preferences: inside study hours, no personal time, session length, daily maximum, breaks, every task scheduled before it's due, and no overlaps.
3. If the AI isn't configured, fails, or breaks a rule, the built-in local planner (earliest deadline first) is used instead. The reasons are listed in `warnings`.
4. Only a plan that passes every check is saved. Every attempt is recorded (see `GET /api/agent/runs`).

**200 OK**

```json
{
  "engine": "claude-haiku-5.5",
  "schedule": [{"title": "CS 3354 Assignment", "assignmentId": "92b7b492-…", "day": "Mon", "start": "16:00", "end": "17:00"}],
  "warnings": []
}
```

| Field | Meaning |
| :--- | :--- |
| `engine` | The AI model that made the plan (e.g. `"claude-haiku-5.5"`, the `OPENROUTER_MODEL` without its provider) or `"local"` |
| `warnings` | Why the AI's plan was rejected, and tasks that only partly fit, e.g. `"Only 60 of 240 min for \"Project\" fit before it is due"` |

**422 Unprocessable Entity**: the saved schedule is left unchanged. Possible messages:
- `"Set your study preferences before generating a schedule"`
- `"You have no unfinished tasks due in the next 7 days"`
- `"None of your study hours are left this week. Add more study hours or try again on Monday."`
- `"Couldn't fit your tasks into your study hours. …"`, with `details` listing each problem

**Errors:** `400` for an invalid `day`.

### `POST /api/agent/chat`

Ask the AI assistant about your tasks and schedule. The assistant sees the student's current and recent tasks and this week's schedule. It can't change any data.

Body: `{"message": "What should I work on first today?"}`. The message must be 1–2000 characters.

**200 OK**

```json
{"reply": "Start with the CS 3354 Assignment: it's high priority and due Thursday…"}
```

**Errors:**
- `400` for an empty or too-long message
- `503` `"The AI assistant isn't set up on this server"` (no AI key)
- `503` `"The AI assistant is unavailable right now. Your tasks and schedule are unaffected."`

### `GET /api/agent/runs`

The student's 20 most recent generation attempts, newest first.

**200 OK**

```json
[
  {"id": "…", "createdAt": "2026-11-23T20:29:00Z", "engine": "claude-haiku-5.5", "ok": false, "blockCount": 0,
   "details": ["\"Essay\" (Sat 2:00 AM-5:00 AM) is outside study hours"]},
  {"id": "…", "createdAt": "2026-11-23T20:29:00Z", "engine": "local", "ok": true, "blockCount": 3, "details": []}
]
```

### `GET /api/agent/replans/{ticket}`

How the background re-plan for an `X-Replan-Ticket` turned out. It answers `done`, `skipped` or `failed` once a re-plan that includes the change behind the ticket has finished; until then it answers `pending`. Clients poll it every few hundred milliseconds.

**200 OK**

```json
{"ticket": 7, "state": "done", "engine": "claude-haiku-5.5", "details": []}
```

```json
{"ticket": 8, "state": "skipped", "message": "None of your study hours are left this week. Add more study hours or try again on Monday.", "details": []}
```

| `state` | Meaning |
| :--- | :--- |
| `pending` | The re-plan hasn't finished yet. |
| `done` | The schedule was rebuilt. `engine` names the planner; `details` lists warnings, such as why the AI's plan was replaced by the built-in planner. |
| `skipped` | Nothing was re-planned; `message` says why (no preferences, no unfinished tasks due in the next 7 days, or no study hours left this week). |
| `failed` | The planner couldn't rebuild the week; the saved schedule is unchanged. `message` and `details` say why. |
| `unknown` | The server doesn't know this ticket, for example because it restarted. |

**Errors:** `400` if the ticket isn't a positive whole number.

---

## Schedule

### Block object

```json
{
  "title": "CS 3354 Assignment",
  "assignmentId": "92b7b492-…",
  "day": "Mon",
  "start": "16:00",
  "end": "17:00",
  "startsAt": "2026-11-23T22:00:00Z",
  "endsAt": "2026-11-23T23:00:00Z"
}
```

| Field | Meaning |
| :--- | :--- |
| `assignmentId` | The task this block is for |
| `day`, `start`, `end` | Local time |
| `startsAt`, `endsAt` | The same block in UTC, for the current week |

### `GET /api/schedule`

**200 OK**

```json
{"weekStart": "2026-11-23T06:00:00Z", "utcOffsetMinutes": -360, "blocks": [ …Block… ]}
```

`weekStart` is the student's local Monday 00:00, expressed in UTC.

### `PATCH /api/schedule/blocks`

Moves one block, and optionally renames it.

```json
{
  "assignmentId": "92b7b492-…",
  "from": {"day": "Mon", "start": "16:00", "end": "17:00"},
  "to":   {"day": "Tue", "start": "18:00", "end": "19:00"},
  "title": "Essay draft"
}
```

- `from` must match exactly one current block for that task.
- The whole schedule must still follow the study preferences after the move.
- Every block must still end before its task is due. Deadlines are read the same way as in [generation](#post-apiagentgenerate): a task that is already overdue, or due after this week ends, counts as due at the end of Sunday. So blocks planned for an overdue task never block a move, while moving a block past a task's real due time later this week is rejected.

**200 OK**: `{"blocks": [ …Block… ]}`, the full updated schedule.

**Errors:**
- `400` for malformed `from` or `to`
- `422` `"That change doesn't fit your study preferences"`, with `details` such as `"\"Essay\" (Sat 10:00 AM-11:00 AM) is outside study hours"`, or `"\"Essay\" (Thu 4:00 PM-5:00 PM) ends after \"Essay\" is due"`
- `422` `"Set your study preferences first"`

### `DELETE /api/schedule/blocks`

Removes one block.

```json
{"assignmentId": "92b7b492-…", "day": "Thu", "start": "10:30", "end": "12:00"}
```

- The block must match exactly one current block for that task.
- Removing a block can't break a study preference, so preferences aren't checked.

**200 OK**: `{"blocks": [ …Block… ]}`, the full updated schedule.

**Errors:**
- `400` for a missing `assignmentId` or a malformed `day`, `start` or `end`
- `404` `"That block isn't on your schedule"`

### `DELETE /api/schedule`

Clears the schedule.

**204 No Content**

### `GET /api/schedule/export.ics`

Downloads an iCalendar (RFC 5545) file. Content type is `text/calendar; charset=utf-8`, with `Content-Disposition: attachment; filename="cramberry-schedule.ics"`. The file contains:
- This week's study blocks, as `Study: <title>` events.
- Every unfinished task due in the future, as `Due: <title>` events.

## Schedule sharing

Schedule sharing is limited to registered recipients. All routes require the normal
`Authorization: Bearer <token>` header. Shared views are read-only and load the
owner's current schedule when requested; recipients cannot use the share ID without
signing in as the invited account. Revoking a share immediately prevents later reads.
The API returns fresh data each time, so a client can refresh the shared view to see
owner edits; push-based live refresh is not provided.

### `POST /api/schedule/shares`

Share the signed-in student's schedule with a registered account:

```json
{"email":"bob@example.com"}
```

**201 Created** returns the share ID, recipient details, and the relative
`viewEndpoint` for the shared view. The recipient gets an in-app notification.
Reauthenticating as the invited account is required to use that endpoint.
Repeating the request for the same recipient returns the existing share and does
not send a duplicate notification.

### `GET /api/schedule/shares`

List recipients who can view the signed-in student's schedule.

### `DELETE /api/schedule/shares/{shareId}`

Revoke a share owned by the signed-in student.

**204 No Content**

### `GET /api/schedule/shared-with-me`

List shares sent to the signed-in student.

### `GET /api/schedule/shared-with-me/{shareId}`

Read the latest schedule for an active share addressed to the signed-in student.
An unknown, revoked, or another student's share returns `404`.

---

## Dashboard

### `GET /api/dashboard`

Everything the home screen needs, in one call.

**200 OK**

```json
{
  "student": { …Student… },
  "today": "Mon",
  "hasPreferences": true,
  "todaysBlocks": [ …Block… ],
  "schedule": [ …Block… ],
  "upcoming": [ …Task… ],
  "overdue": [ …Task… ],
  "counts": {"notStarted": 4, "inProgress": 1, "completedThisWeek": 2}
}
```

| Field | Contains |
| :--- | :--- |
| `today` | The student's local day of the week |
| `upcoming` | Up to 10 unfinished tasks due in the next 14 days, highest priority first, then earliest due |
| `overdue` | All unfinished overdue tasks |
| `counts.completedThisWeek` | Tasks completed since the student's local Monday 00:00 |

---

## Notifications

### Notification settings object

```json
{
  "enabled": true,
  "email": true,
  "desktop": true,
  "sms": false,
  "phoneNumber": "",
  "deadlineReminders": true,
  "deadlineLeadMinutes": 1440
}
```

| Field | Meaning |
| :--- | :--- |
| `enabled` | Master switch |
| `email`, `desktop`, `sms` | Which channels to use |
| `deadlineLeadMinutes` | How long before a deadline to remind |

### `GET /api/settings/notifications`

**200 OK**: the settings object. Defaults if none are saved: everything off, `deadlineLeadMinutes` 1440 (1 day).

### `PUT /api/settings/notifications`

Body: a settings object. Missing fields take the defaults.

**Validation (400):**
- `"Enter a valid phone number"`: when `sms` is on, the number needs 10–15 digits. A leading `+`, spaces, `-` and `()` are allowed.
- `"Choose when to be reminded before a deadline"`: `deadlineReminders` is on but `deadlineLeadMinutes` ≤ 0.
- `"Turn on at least one notification source"`: `enabled` is on with no channel selected.

**200 OK**

```json
{"message": "Notification settings saved.", "settings": { …settings… }}
```

### How reminders are sent

Once a minute, the server checks every unfinished task due in the next 14 days. A reminder goes out when the student has `enabled` and `deadlineReminders` on, and the current time is between `dueAt − deadlineLeadMinutes` and `dueAt`:
- One reminder per channel that's turned on.
- Subject: `Reminder: <title> is due soon`.
- Each task gets at most one reminder per channel while the server is running.

All reminders are stored in the in-app inbox below. Email and SMS delivery needs a provider to be connected; until then those reminders are logged by the server and kept in the inbox.

### `GET /api/notifications?unread=true`

The student's 100 newest notifications, newest first. Add `unread=true` to list only unread ones.

**200 OK**

```json
[
  {"id": "…", "channel": "desktop", "subject": "Reminder: Essay is due soon",
   "body": "Essay is due 2026-11-24 12:00 UTC.", "sentAt": "2026-11-23T12:00:00Z", "read": false}
]
```

### `POST /api/notifications/{id}/read`

Marks one notification as read.

**204 No Content**

**Errors:** `404` `"Notification not found"`.

### `POST /api/notifications/read-all`

Marks every notification as read.

**204 No Content**

---

## Status codes

| Code | Meaning |
| :--- | :--- |
| `200` | OK |
| `201` | Created (register, new course, new task) |
| `204` | Done, no body |
| `400` | Invalid input; `error` says what to fix |
| `401` | Not signed in or session expired; wrong credentials or password |
| `404` | No such item for this student, or no such endpoint (`"No such endpoint"`) |
| `409` | Email already registered; personal time conflicts with the schedule |
| `422` | Valid request that can't be carried out (no preferences, no tasks, plan doesn't fit, move breaks preferences) |
| `500` | Unexpected server error |
| `503` | AI assistant not configured or unavailable; database down (health) |
