# Implementation Notes: Ahnaf Adib (AA)

**Use cases implemented:** UC-10 Process Scheduling Request, UC-11 Save/Retrieve Student Data, UC-12 Validate AI Response
**Branch:** `feature/aa-scheduling-request-data-ai-validation`
**Test cases and RTM:** [`docs/testing/AA_Test_Cases.md`](testing/AA_Test_Cases.md)

## Contribution

All of the files below are by Ahnaf Adib (AA). Each one starts with a comment naming its use case and purpose.

| File | Use case | Purpose |
| :--- | :--- | :--- |
| `backend/api/SchedulingRequestController.{h,cpp}` | UC-10 | Validates a scheduling request (fields, dates, types), loads task details, prepares the request and forwards it to the Schedule System. |
| `backend/api/StudentDataController.{h,cpp}` | UC-11 | Saves, retrieves and updates a student's records after checking they own the record. |
| `backend/data/StudentDataRepository.h` | UC-11 | Storage interface for student records (Supabase in production). |
| `backend/agent/ScheduleGenerationService.{h,cpp}` | UC-12 | Asks Gemini for a schedule, checks the JSON reply (fields, times, deadlines, overlaps) and saves it; keeps the old schedule if anything fails. |
| `backend/schedule/SchedulingTypes.h` | Shared | Strict date/time parsing and the types passed from UC-10 to UC-12. |
| `backend/tests/test_aa.cpp` | Tests | 5 GoogleTest cases (3 functional, 2 non-functional) with in-memory fakes. |
| `backend/CMakeLists.txt` (edited) | Build | Adds the files above and the nlohmann/json library. |

## Design Class Diagram: AA Additions

Compared with the Phase 2 class diagram, these classes are new. They are all backend classes, which Phase 2 didn't model; Phase 2 only had `AIAssistant.validateResponse()`.

| Class | Stereotype | Attributes | Methods | Relationships |
| :--- | :--- | :--- | :--- | :--- |
| `SchedulingRequestController` | «controller» | – | `processRequest(studentId, input, now): ProcessResult`, `validate(input, now): List<FieldError>`, `prepareRequest(...): PreparedSchedulingRequest` | uses `ITaskDetailsSource`, `IScheduleSystem` |
| `SchedulingRequestInput` | «DTO» | `tasks: List<TaskInput>`, `prefs: StudyPreferences` | – | contains 1..* `TaskInput` |
| `TaskInput` | «DTO» | `taskId: String`, `dueDate: String`, `estimatedMinutes: Integer` | – | – |
| `FieldError` | «value» | `field: String`, `message: String` | – | – |
| `PreparedSchedulingRequest` | «DTO» | `studentId`, `tasks: List<TaskDetails>`, `prefs`, `submittedAt` | – | sent from UC-10 to UC-12 |
| `StudentDataController` | «controller» | – | `getRecord()`, `listRecords()`, `createRecord()`, `updateRecord()`, `validateFields()` | uses `IStudentDataRepository` |
| `StudentRecord` | «entity» | `id`, `ownerId`, `type: RecordType`, `fields` | – | owned by 1 `Student` |
| `IStudentDataRepository` | «interface» | – | `findById()`, `listByOwner()`, `insert()`, `update()` | implemented by the Supabase adapter |
| `AIResponseValidator` | «service» | – | `validateAIResponse(raw, request): AIValidationResult` | used by `ScheduleGenerationService`; in code it's a function in `ScheduleGenerationService.cpp` |
| `ScheduleGenerationService` | «service» | – | `generateSchedule(request): GenerationResult` | uses `IGeminiClient`, `IGeneratedScheduleRepository`, `AIResponseValidator`; produces 0..* `ScheduledEvent` |
| `ScheduledEvent` | «entity» | `assignmentId`, `title`, `start`, `end` | – | implements `ScheduleEvent` from Phase 2 |

**Diagram files:** [`docs/design/Group08_Design_Class_Diagram_Phase4.png`](design/Group08_Design_Class_Diagram_Phase4.png) (editable source: `.drawio` in the same folder). Phase 2 classes are lilac. Classes added in Phase 4 are pink.

**Association class:** `SchedulingRequest` sits on the new **Student 1 —requests— 0..\* Schedule** association. Each time a student asks for a schedule, the link records the request ID, the tasks, a copy of the preferences and the submission time. UC-10 creates it and UC-12 turns it into a Schedule. In the code it's `PreparedSchedulingRequest`.

**Changes from Phase 2 to explain in the document:**
- **Added:** the association class `SchedulingRequest` on a new Student–Schedule "requests" association. Phase 2 had no record of a schedule request.
- **Validation moved.** `AIAssistant.validateResponse(r): Boolean` became `AIResponseValidator`. It returns every problem it finds, not just true/false, so the user and the logs can see why a schedule was rejected.
- **New controller layer.** Controllers (`SchedulingRequestController`, `StudentDataController`) now sit between the UI and the entities, matching the Backend API System lifeline in the sequence diagrams.
- **Storage behind interfaces.** Each storage and AI dependency is an interface (`IStudentDataRepository`, `IGeminiClient`, and so on), the same pattern as the team's existing `ISettingsRepository`. Production code uses Supabase or Gemini; tests use fakes.

## AI-Assisted Development

**Tools used:** Claude Code (Anthropic, Claude Opus 5.5), run in the terminal inside the project folder with access to the repo, the diagrams and the SE Document. Codex as a second model to cross-check the work.

**Example prompts:**
- "Review the schedule diagram."
- "Review the sequence diagram."
- "Review my teammates' code."
- "Review the overall structure that there needs to be."

**How the generated code was checked and changed:**
- **Matched to the repo.** The AI first proposed a new Next.js project. After the team repo was cloned, it turned out the backend is C++ with GoogleTest and repository interfaces, so the code was rewritten to follow that pattern and reuse `SettingsController::validate()` for preference checks.
- **Matched to the design.** Each method was checked against the AA sequence diagrams (validate → getTaskDetails → prepareRequest → forwardRequest; checkAccess → select/update; parseResponse → checkFieldsAndFormat → saveSchedule), including the exact status codes and messages.
- **Built and run.** All 5 AA tests pass. The output is saved in `docs/testing/AA-results/`. The 9 tests already on `main` (UC-15 and UC-16, by Joshua) were also run, only to confirm the AA changes didn't break them, and they still pass.
- **Tests checked by breaking the code.** Three bugs were introduced on purpose: the ownership check removed, the overlap check removed, and the UC-10 "drop other students' rows" guard removed. The first two were caught by tests. The third wasn't, because the fake task source already filtered by student. A new test with a deliberately broken task source was added to TC-NFR19-01, and the bug is now caught.

**My own review (Ahnaf Adib):**
- **How I split the work.** The AI wrote the code and the test cases. I gave it the references and checked what came back.
- **References I gave it.** I gave the AI my sequence diagrams for UC-10, UC-11 and UC-12, the use case descriptions and the assignment PDF. I drew those diagrams, so I could tell where the code drifted from them.
- **Test cases checked against the diagrams.** For each test case, I compared the inputs, steps and expected messages with the alternate flows in my sequence diagrams. Another student's record and an AI schedule with overlapping or late events each had to produce the error my diagram shows.
- **How I worked.** I reviewed the diagram images first, then the code.
- **Each test case checked twice.** After each test case, I verified it myself and then had Codex check it as well, so a second model reviewed the work.
- **Tests run on my machine.** I built the project on my laptop, ran my 5 tests and compared the output with the expected results.
- **What I learned.** When I asked the AI to build UC-10 with nothing else to go on, it picked Next.js, the wrong stack. After I gave it the team repo and my diagrams, its code matched our design and needed few fixes.
