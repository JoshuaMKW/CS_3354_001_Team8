# Functional test cases (NK)

Test author: Nishanth Kandula (NK). Actual results and Pass/Fail are in [results.md](results.md).

## UC-5 Generate AI Study Schedule

### TC-05-01 Generate a Conflict-Free Weekly Plan

| Field | Value |
| :--- | :--- |
| Test case ID | TC-05-01 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-5 Generate AI Study Schedule |
| Flow | Successful flow |
| Purpose | Verify AI builds a feasible weekly study plan using saved tasks, deadlines, and availability. |
| Preconditions | Student logged in; tasks saved; AI API and database available. |
| Test data | CS 3354 Project: 2 hrs, due Fri 11:59 PM; Math Quiz: 1 hr, due Thu 10 AM; free Mon-Thu 4-7 PM; Tue class 5-6 PM. |
| Steps | 1. Open Study Planner. 2. Select the test week. 3. Click Generate New Plan. 4. Review and accept the draft. |
| Expected result | Study blocks fit available hours and occur before deadlines. None overlap Tue 5-6 PM class. Accepted plan appears on calendar. |

### TC-05-02 Insufficient Time Before Deadline

| Field | Value |
| :--- | :--- |
| Test case ID | TC-05-02 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-5 Generate AI Study Schedule |
| Flow | Alternative flow |
| Purpose | Verify AI handles a workload that cannot fit before the deadline without creating conflicts. |
| Preconditions | Student logged in; an accepted schedule exists; AI and calendar systems available. |
| Test data | Physics Exam Prep: 4 hrs, due Fri 9 AM; only free study time before deadline is Thu 4-5 PM. |
| Steps | 1. Open Study Planner. 2. Apply the test availability. 3. Generate a new plan. 4. Review any warning and the saved plan. |
| Expected result | System reports that 4 hrs cannot fit in the 1-hr free window and requests changed constraints or workload. No overlapping block is accepted; saved plan stays unchanged. |

### TC-05-03 Reject Reversed Availability Hours

| Field | Value |
| :--- | :--- |
| Test case ID | TC-05-03 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-5 Generate AI Study Schedule |
| Flow | Invalid input |
| Purpose | Verify invalid study availability is rejected before AI generates a schedule. |
| Preconditions | Student logged in; current plan saved; study preferences are editable. |
| Test data | Study availability Monday from 7 PM to 4 PM (end time before start); BIO Lab due Fri. |
| Steps | 1. Open Study Settings. 2. Enter the invalid Monday hours. 3. Attempt to save and generate a new plan. 4. Reload. |
| Expected result | Validation explains that end time must be after start. Invalid hours are not saved or used for generation; the accepted plan remains intact. |

### TC-05-04 Sync Tasks, Calendar, and AI Schedule

| Field | Value |
| :--- | :--- |
| Test case ID | TC-05-04 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-5 Generate AI Study Schedule |
| Flow | Integration |
| Purpose | Verify a generated plan uses task and calendar data and stays saved across systems. |
| Preconditions | Student logged in; Task, Calendar, Schedule, AI, and database systems available. |
| Test data | ENG 1301 Essay: 90 min, due Fri; CS 3354 Lab: 60 min, due Thu; class Wed 4-5 PM; free Mon-Fri 4-7 PM. |
| Steps | 1. Save both tasks. 2. Add the fixed class event. 3. Generate and accept a study plan. 4. Reload Tasks and Calendar. |
| Expected result | Generated study times account for both tasks and their deadlines, do not overlap Wed 4-5 PM class, and remain visible after reload. Task deadlines are unchanged. |

## UC-7 Interact with AI Chatbot

### TC-07-01 Answer a Saved Task Question

| Field | Value |
| :--- | :--- |
| Test case ID | TC-07-01 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-7 Interact with AI Chatbot |
| Flow | Successful flow |
| Purpose | Verify chatbot answers a question using the student's saved assignment information. |
| Preconditions | Student logged in; AI API available; task stored in Task Management System. |
| Test data | Saved task "CS 3354 Project," due Fri 11:59 PM; prompt: "When is my CS 3354 Project due?" |
| Steps | 1. Open AI Chatbot. 2. Send the prompt. 3. Read the reply. 4. Compare the reply with the saved task. |
| Expected result | Reply identifies CS 3354 Project and its Fri 11:59 PM deadline accurately. No tasks or schedule blocks change. |

### TC-07-02 Move a Study Block Through Chat

| Field | Value |
| :--- | :--- |
| Test case ID | TC-07-02 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-7 Interact with AI Chatbot |
| Flow | Alternative flow |
| Purpose | Verify a student can use a chatbot command to change an existing study time. |
| Preconditions | Student logged in; "CS 3354 Review" is scheduled Tue 4-5 PM; Wed 4-5 PM is free. |
| Test data | Prompt: "Move CS 3354 Review from Tuesday 4-5 PM to Wednesday 4-5 PM." |
| Steps | 1. Open AI Chatbot. 2. Send the request. 3. Confirm if prompted. 4. Check Calendar and reload the page. |
| Expected result | One CS 3354 Review block appears Wed 4-5 PM, not Tue 4-5 PM. Other events remain unchanged; the new time persists after reload. |

### TC-07-03 Reject a Nonexistent Task Update

| Field | Value |
| :--- | :--- |
| Test case ID | TC-07-03 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-7 Interact with AI Chatbot |
| Flow | Invalid input |
| Purpose | Verify chatbot does not edit or invent a task that does not exist in the student's records. |
| Preconditions | Student logged in; task list has no "HIST 3300 Essay"; current schedule saved. |
| Test data | Prompt: "Move HIST 3300 Essay to tomorrow at 5 PM." |
| Steps | 1. Open AI Chatbot. 2. Send the prompt. 3. Check the reply. 4. Inspect tasks and reload Calendar. |
| Expected result | Chatbot reports the task cannot be found or asks for a valid task. No task is fabricated, and the saved schedule is unchanged. |

### TC-07-04 Create and Schedule a Task via Chat

| Field | Value |
| :--- | :--- |
| Test case ID | TC-07-04 |
| Test author | Nishanth Kandula (NK) |
| Use case | UC-7 Interact with AI Chatbot |
| Flow | Integration |
| Purpose | Verify a single chat request updates both Task Management and Schedule systems. |
| Preconditions | Student logged in; AI API and database available; Wed 4-5 PM free. |
| Test data | Prompt: "Add CHEM 1311 Chapter 5 Review due Fri 11:59 PM and schedule 60 minutes Wed 4-5 PM." |
| Steps | 1. Open AI Chatbot. 2. Send the prompt. 3. Confirm if prompted. 4. Open Tasks and Calendar, then reload. |
| Expected result | Exactly one task has the correct title and due time; one Wed 4-5 PM study block exists without conflict. Both persist after reload. |
