# Cramberry Backend (C++)

The folder layout follows the Development View: `agent/`, `schedule/`, `settings/` and `notifications/`, with each controller depending on a repository interface. Production repositories would talk to Supabase. The tests replace them with in-memory fakes in [`tests/Fakes.h`](tests/Fakes.h), so they never touch a real student's data, Gemini or the Notification Service.

## Tests

| Test file | Use case | Test cases |
| :--- | :--- | :--- |
| [`tests/test_manage_study_preferences.cpp`](tests/test_manage_study_preferences.cpp) | UC-15 Manage Study Preferences | TC-15-01 to TC-15-04 |
| [`tests/test_notification_settings.cpp`](tests/test_notification_settings.cpp) | UC-16 Update Notification Settings | TC-16-01 to TC-16-04 |
| [`tests/test_aa.cpp`](tests/test_aa.cpp) | UC-10, UC-11, UC-12, NFR-18, NFR-19 | TC-10-01, TC-11-01, TC-12-01, TC-NFR18-01, TC-NFR19-01 |

The test cases themselves are written up in [`docs/testing/JA_Test_Cases.md`](../docs/testing/JA_Test_Cases.md) and [`docs/testing/AA_Test_Cases.md`](../docs/testing/AA_Test_Cases.md).

### AA modules (Ahnaf Adib)

| Module | Use case | Purpose |
| :--- | :--- | :--- |
| `api/SchedulingRequestController` | UC-10 | `POST /api/schedule`: validates tasks, deadlines and preferences, then forwards the request to the Schedule System. |
| `api/StudentDataController`, `data/StudentDataRepository.h` | UC-11 | `GET`/`POST`/`PATCH /api/data`: saves, retrieves and updates a student's records after checking they own them. |
| `agent/ScheduleGenerationService` | UC-12 | Asks Gemini for a schedule, checks the JSON reply (fields, times, deadlines, overlaps) and saves it; keeps the old schedule if anything fails. |
| `schedule/SchedulingTypes.h` | Shared | Date/time parsing and the types passed from UC-10 to UC-12. |

UC-12 uses [nlohmann/json](https://github.com/nlohmann/json) v3.11.3, which CMake downloads on the first configure along with GoogleTest.

## Build and run

You need CMake 3.20 or later and a C++20 compiler (g++ 11+, clang 14+ or MSVC 2022). GoogleTest v1.15.2 is downloaded automatically the first time you configure.

```bash
cmake -S backend -B backend/build
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
```
