cmake_minimum_required(VERSION 3.20)
project(CramberryBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cramberry_backend
    agent/Agent.cpp
    agent/ScheduleGenerationService.cpp
    api/SchedulingRequestController.cpp
    api/StudentDataController.cpp
    notifications/NotificationController.cpp
    notifications/ReminderScheduler.cpp
    settings/SettingsController.cpp
)
target_include_directories(cramberry_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# GoogleTest is downloaded on the first configure.
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # needed for MSVC
# nlohmann/json parses Gemini's JSON reply in UC-12 Validate AI Response (AA).
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest nlohmann_json)
target_link_libraries(cramberry_backend PRIVATE nlohmann_json::nlohmann_json)

enable_testing()

add_executable(cramberry_tests
    tests/test_manage_study_preferences.cpp
    tests/test_notification_settings.cpp
    tests/test_aa.cpp
)
target_link_libraries(cramberry_tests PRIVATE cramberry_backend GTest::gmock_main)

include(GoogleTest)
gtest_discover_tests(cramberry_tests)
