cmake_minimum_required(VERSION 3.20)
project(CramberryBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cramberry_backend
    tasks/TaskController.cpp
    agent/Agent.cpp
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
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(cramberry_tests
    tests/test_rm_tasks.cpp
    tests/test_manage_study_preferences.cpp
    tests/test_notification_settings.cpp
)
target_link_libraries(cramberry_tests PRIVATE cramberry_backend GTest::gmock_main)

include(GoogleTest)
gtest_discover_tests(cramberry_tests)
