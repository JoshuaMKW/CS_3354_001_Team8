# NK live test results: UC-5 and UC-7

| Item | Value |
| :--- | :--- |
| Date | Fri 27 Nov 2026, 23:28 CST |
| Tester | Ahnaf Adib (AA) |
| Test author | Nishanth Kandula (NK) |
| Machine | Darwin 27.0.0 (arm64) |
| Server | `cramberry_server` from `backend/out/build`, one server on port 8102 with `CRAMBERRY_AUTO_REPLAN=0`, test students on UTC-6 |
| Database | Supabase hosted PostgreSQL (db.<project>.supabase.co:5432) |
| AI | `anthropic/claude-haiku-5.5` through OpenRouter |
| Client | Python 3 `urllib`, one request at a time |

**8 run, 4 Pass, 4 Fail.**

| Test case ID | Requirement / use case | Test author | Tester | Inputs | Expected result | Actual result | Pass/Fail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TC-05-01 Generate a conflict-free weekly plan | UC-5 Generate AI Study Schedule, FR-16 (AI builds daily and weekly study schedules from tasks, deadlines and availability) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Study preferences (PUT /api/settings/preferences, Study Settings page): free Saturday Nov 28 and Sunday Nov 29 4-7 PM. Class Saturday Nov 28 5-6 PM entered as personal time. 60 min sessions, 15 min breaks, 180 min a day. Tasks: Math Quiz 1 hr due Sunday Nov 29 10 AM, CS 3354 Project 2 hrs due Sunday Nov 29 11:59 PM. Generate New Plan is POST /api/agent/generate, then the calendar is reloaded with GET /api/schedule. | Study blocks fit available hours and occur before deadlines. None overlap the class. Accepted plan appears on calendar. | 200, engine claude-haiku-5.5, warnings none, 2.5 s. Saved blocks Math Quiz Sat 4 PM-5 PM, CS 3354 Project Sat 6 PM-7 PM, CS 3354 Project Sun 4 PM-5 PM; minutes {'Math Quiz': 60, 'CS 3354 Project': 120}; rule problems none; study_blocks rows match. | **Pass** |
| TC-05-02 Insufficient time before deadline | UC-5 Generate AI Study Schedule, FR-16 (AI builds daily and weekly study schedules from tasks, deadlines and availability) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Accepted plan first: free Sunday Nov 29 4-7 PM, task Existing task 1 hr due Sunday Nov 29 11:59 PM, generated and saved. Test availability then saved: free Saturday Nov 28 4-5 PM and Sunday Nov 29 4-7 PM. Task Physics Exam Prep 4 hrs due Sunday Nov 29 9 AM, so the only free time before its deadline is Saturday Nov 28 4-5 PM. The saved plan is the calendar right before Generate, after the rebuild that saving preferences starts. Generate is POST /api/agent/generate. | System reports that 4 hrs cannot fit in the 1-hr free window and requests changed constraints or workload. No overlapping block is accepted; saved plan stays unchanged. | 200, engine local, messages "claude-haiku-5.5: "Existing task" is not scheduled Only 60 of 240 min for "Physics Exam Prep" fit before it is due". Plan before Existing task Sat 4 PM-5 PM, Existing task Sun 4 PM-5 PM; after Physics Exam Prep Sat 4 PM-5 PM, Existing task Sun 4 PM-5 PM. | **Fail** |
| TC-05-03 Reject reversed availability hours | UC-5 Generate AI Study Schedule, FR-16 (AI builds daily and weekly study schedules from tasks, deadlines and availability) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Accepted plan first: free Sunday Nov 29 4-5 PM, task BIO Lab 1 hr due Sunday Nov 29 11:59 PM, generated and saved. Then study availability Saturday Nov 28 from 7 PM to 4 PM added and saved with PUT /api/settings/preferences (the Study Settings grid cannot paint a reversed range, so the request is sent as the page would send it), then POST /api/agent/generate, then preferences and calendar reloaded. | Validation explains that end time must be after start. Invalid hours are not saved or used for generation; the accepted plan remains intact. | Save 400 "Study hours must end after they start"; generate 200. Saved hours ['Sun 16:00-17:00']; plan before BIO Lab Sun 4 PM-5 PM, after BIO Lab Sun 4 PM-5 PM. | **Pass** |
| TC-05-04 Sync tasks, calendar, and AI schedule | UC-5 Generate AI Study Schedule, FR-16 (AI builds daily and weekly study schedules from tasks, deadlines and availability) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Tasks saved with POST /api/tasks (Tasks page): ENG 1301 Essay 90 min due Sunday Nov 29 11:59 PM, CS 3354 Lab 60 min due Saturday Nov 28 11:59 PM. Fixed class Saturday Nov 28 4-5 PM entered as personal time. Free Saturday Nov 28 and Sunday Nov 29 4-7 PM. Generate is POST /api/agent/generate, then GET /api/tasks and GET /api/schedule as the reload. | Generated study times account for both tasks and their deadlines, do not overlap the Saturday Nov 28 4-5 PM class, and remain visible after reload. Task deadlines are unchanged. | 200, engine claude-haiku-5.5. Blocks after reload CS 3354 Lab Sat 5 PM-6 PM, ENG 1301 Essay Sat 6:15 PM-7 PM, ENG 1301 Essay Sun 4 PM-5 PM, ENG 1301 Essay Sun 5:15 PM-6:15 PM; minutes {'CS 3354 Lab': 60, 'ENG 1301 Essay': 165}; rule problems none; study_blocks rows match. | **Pass** |
| TC-07-01 Answer a saved task question | UC-7 Interact with AI Chatbot, FR-20 (chatbot answers questions about assignments and schedules) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Saved task CS 3354 Project due Sunday Nov 29 11:59 PM, with a generated plan on the calendar. Prompt "When is my CS 3354 Project due?" sent with POST /api/agent/chat, the message box on the AI Assistant page. | Reply identifies CS 3354 Project and its Sunday Nov 29 11:59 PM deadline accurately. No tasks or schedule blocks change. | 200, reply "Your CS 3354 Project is due on 2026-11-30 at 05:59 UTC. Its status is Not Started, and it's estimated at 60 minutes.". Tasks and blocks unchanged: True. | **Fail** |
| TC-07-02 Move a study block through chat | UC-7 Interact with AI Chatbot, FR-20 (chatbot as a shortcut to create tasks or edit the schedule) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | CS 3354 Review scheduled Saturday Nov 28 4-5 PM and Other task Saturday Nov 28 6-7 PM (placed with PATCH /api/schedule/blocks, Edit Schedule); Sunday Nov 29 4-5 PM is free study time. Prompt "Move CS 3354 Review from Saturday 4-5 PM to Sunday 4-5 PM." sent with POST /api/agent/chat, the message box on the AI Assistant page. A yes is sent if the reply asks to confirm. Then GET /api/schedule as the reload. | One CS 3354 Review block appears Sunday Nov 29 4-5 PM, not Saturday Nov 28 4-5 PM. Other events remain unchanged; the new time persists after reload. | 200, reply "I can't move it myself, since I can only read your schedule. To change it, go to the schedule page in Cramberry and edit the Saturday 4:00 PM block for CS 3354 Review, setting it to Sunday 4:00 PM-5:00 PM. One thing to c...". CS 3354 Review after reload ['Sat 16:00-17:00']. | **Fail** |
| TC-07-03 Reject a nonexistent task update | UC-7 Interact with AI Chatbot, FR-20 (chatbot as a shortcut to create tasks or edit the schedule) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Task list has only CS 3354 Project, with a saved plan. Prompt "Move HIST 3300 Essay to tomorrow at 5 PM." sent with POST /api/agent/chat, the message box on the AI Assistant page. Then GET /api/tasks and GET /api/schedule as the reload. | Chatbot reports the task cannot be found or asks for a valid task. No task is fabricated, and the saved schedule is unchanged. | 200, reply "I can't make that change. I can only answer questions about your tasks and schedule, not edit them. To move the HIST 3300 Essay to tomorrow at 5 PM, use the schedule page in Cramberry and edit that study block there. Als...". Tasks after ['CS 3354 Project']; blocks unchanged: True. | **Pass** |
| TC-07-04 Create and schedule a task via chat | UC-7 Interact with AI Chatbot, FR-20 (chatbot as a shortcut to create tasks or edit the schedule) | Nishanth Kandula (NK) | Ahnaf Adib (AA) | Study preferences free Saturday Nov 28 4-5 PM and Sunday Nov 29 4-7 PM, no tasks, empty calendar. Prompt "Add CHEM 1311 Chapter 5 Review due Sun 11:59 PM and schedule 60 minutes Sat 4-5 PM." sent with POST /api/agent/chat, the message box on the AI Assistant page. A yes is sent if the reply asks to confirm. Then GET /api/tasks and GET /api/schedule as the reload. | Exactly one task has the correct title and due time; one Saturday Nov 28 4-5 PM study block exists without conflict. Both persist after reload. | 200, reply "I can't add tasks or schedule blocks myself, since I only have read access to your data. To do this, go to the Tasks page to add "CHEM 1311 Chapter 5 Review" with a due date of Sunday at 11:59 PM, then go to the Study Sc...". Tasks after reload none; blocks none. | **Fail** |

## Checks

**TC-05-01** (Pass)
- ok: generate answers 200
- ok: both saved tasks get study blocks
- ok: every block is inside the free hours, before its deadline, off the class hour, with no overlaps
- ok: calendar after reload shows the plan that was returned
- ok: study_blocks rows in Supabase match the calendar

**TC-05-02** (Fail)
- ok: the answer says Physics Exam Prep does not fit before its deadline
- FAILED: the answer asks for more study time or less work
- ok: no overlapping or out-of-hours block is saved
- FAILED: saved plan is unchanged (API)
- FAILED: saved plan is unchanged (study_blocks rows)

**TC-05-03** (Pass)
- ok: save is rejected with 400
- ok: message says the end time must be after the start
- ok: reloaded preferences have no hours on the invalid day
- ok: study_preferences row in Supabase is unchanged
- ok: no block uses the invalid hours
- ok: accepted plan remains intact after the generate attempt

**TC-05-04** (Pass)
- ok: generate answers 200
- ok: ENG 1301 Essay gets at least its 90 min and CS 3354 Lab at least its 60 min
- ok: every block is before its task's deadline, inside free hours, off the class hour, with no overlaps
- ok: blocks are visible after reload and match study_blocks rows
- ok: task deadlines unchanged (API)
- ok: task deadlines unchanged (tasks rows)

**TC-07-01** (Fail)
- ok: chat answers 200
- ok: reply names CS 3354 Project
- FAILED: reply gives the due day (Sunday Nov 29)
- FAILED: reply gives the due time 11:59 PM
- ok: tasks rows unchanged
- ok: study_blocks rows unchanged

**TC-07-02** (Fail)
- ok: chat answers 200
- FAILED: exactly one CS 3354 Review block, at Sun 16:00-17:00
- FAILED: no CS 3354 Review block at Sat 16:00-17:00
- ok: other events unchanged
- FAILED: new time persists in study_blocks rows

**TC-07-03** (Pass)
- ok: chat answers 200
- ok: reply says the task can't be found or asks for a valid task
- ok: no HIST 3300 Essay task exists
- ok: tasks rows unchanged
- ok: study_blocks rows unchanged

**TC-07-04** (Fail)
- ok: chat answers 200
- FAILED: exactly one CHEM 1311 Chapter 5 Review task, due Sunday Nov 29 11:59 PM
- FAILED: exactly one block for it, at Sat 16:00-17:00
- FAILED: task and block persist in Supabase rows
