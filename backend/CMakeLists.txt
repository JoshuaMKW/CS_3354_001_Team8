cmake_minimum_required(VERSION 3.20)
project(CramberryBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CRAMBERRY_BUILD_SERVER "Build the Drogon REST API server (needs Drogon, e.g. from vcpkg)" ON)

# Use-case logic with no third-party dependencies. Builds everywhere.
add_library(cramberry_backend
    tasks/RMTaskController.cpp
    agent/Agent.cpp
    agent/ScheduleGenerationService.cpp
    api/SchedulingRequestController.cpp
    api/StudentDataController.cpp
    agent/NKStudyScheduleService.cpp
    notifications/NotificationController.cpp
    notifications/ReminderScheduler.cpp
    schedule/NKScheduleEditor.cpp
    schedule/NKScheduleRules.cpp
    settings/SettingsController.cpp
    schedule/UploadSchedule.cpp
    schedule/ViewSchedule.cpp
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
include(GoogleTest)

add_executable(cramberry_tests
    tests/test_rm_tasks.cpp
    tests/test_manage_study_preferences.cpp
    tests/test_notification_settings.cpp
    tests/test_aa.cpp
    tests/test_jo_schedule.cpp
)
target_link_libraries(cramberry_tests PRIVATE cramberry_backend GTest::gmock_main)
gtest_discover_tests(cramberry_tests)

# ---------------------------------------------------------------------------
# REST API (Drogon). Configure with the vcpkg toolchain to get Drogon:
#   cmake -S backend -B backend/build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# ---------------------------------------------------------------------------
if(CRAMBERRY_BUILD_SERVER)
    find_package(Drogon CONFIG QUIET)
    find_package(OpenSSL QUIET)
endif()

if(CRAMBERRY_BUILD_SERVER AND Drogon_FOUND AND OpenSSL_FOUND)
    add_library(cramberry_api
        agent/AgentController.cpp
        agent/AgentRepository.cpp
        agent/AgentRoutes.cpp
        agent/GeminiClient.cpp
        agent/Planning.cpp
        common/Http.cpp
        common/Json.cpp
        data-api/Database.cpp
        data-api/Transaction.cpp
        notifications/NotificationRepository.cpp
        notifications/NotificationRoutes.cpp
        schedule/ScheduleController.cpp
        schedule/ScheduleRepository.cpp
        schedule/ScheduleRoutes.cpp
        schedule/ScheduleShareRepository.cpp
        schedule/ScheduleSharing.cpp
        schedule/ScheduleSharingRoutes.cpp
        settings/SettingsRepository.cpp
        settings/SettingsRoutes.cpp
        tasks/Task.cpp
        tasks/TaskController.cpp
        tasks/TaskRepository.cpp
        tasks/TaskRoutes.cpp
        user/AuthGuard.cpp
        user/Crypto.cpp
        user/UserController.cpp
        user/UserRepository.cpp
        user/UserRoutes.cpp
    )
    target_link_libraries(cramberry_api PUBLIC cramberry_backend Drogon::Drogon OpenSSL::Crypto)
    if(MSVC)
        target_compile_options(cramberry_api PUBLIC /utf-8 /bigobj)
    endif()

    add_executable(cramberry_server main.cpp)
    target_link_libraries(cramberry_server PRIVATE cramberry_api)

    add_executable(cramberry_api_tests
        tests/test_api_support.cpp
        tests/test_local_planner.cpp
        tests/test_repositories.cpp
        tests/test_tasks.cpp
        tests/test_users.cpp
        tests/test_re_login.cpp
    )
    target_link_libraries(cramberry_api_tests PRIVATE cramberry_api GTest::gmock_main)
    gtest_discover_tests(cramberry_api_tests DISCOVERY_TIMEOUT 60)

    add_executable(cramberry_schedule_acceptance_tests
        tests/test_schedule_acceptance.cpp
    )
    target_link_libraries(cramberry_schedule_acceptance_tests PRIVATE cramberry_api GTest::gmock_main)
    gtest_discover_tests(cramberry_schedule_acceptance_tests DISCOVERY_TIMEOUT 60)
elseif(CRAMBERRY_BUILD_SERVER)
    message(WARNING "Drogon or OpenSSL not found: building the use-case tests only. "
                    "See backend/README.md to build the REST API server.")
endif()
