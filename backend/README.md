# Cramberry Backend (C++)

The folder layout follows the Development View: `agent/`, `schedule/`, `settings/` and `notifications/`, with each controller depending on a repository interface. Production repositories would talk to Supabase. The tests replace them with in-memory fakes in [`tests/Fakes.h`](tests/Fakes.h), so they never touch a real student's data, Gemini or the Notification Service.

## Tests

| Test file | Use case | Test cases |
| :--- | :--- | :--- |
| [`tests/test_manage_study_preferences.cpp`](tests/test_manage_study_preferences.cpp) | UC-15 Manage Study Preferences | TC-15-01 to TC-15-04 |
| [`tests/test_notification_settings.cpp`](tests/test_notification_settings.cpp) | UC-16 Update Notification Settings | TC-16-01 to TC-16-04 |

The test cases themselves are written up in [`docs/testing/JA_Test_Cases.md`](../docs/testing/JA_Test_Cases.md).

## Build and run

You need CMake 3.20 or later and a C++20 compiler (g++ 11+, clang 14+ or MSVC 2022). GoogleTest v1.15.2 is downloaded automatically the first time you configure.

```bash
cmake -S backend -B backend/build
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
```
