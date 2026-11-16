cmake_minimum_required(VERSION 3.20)
project(cramberry_auth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CRAMBERRY_AUTH_BUILD_SERVER "Build the demo server (backend/main.cpp)" ON)
option(CRAMBERRY_AUTH_BUILD_TESTS  "Build the GoogleTest tests"               ON)

find_package(SQLite3 REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# The reusable part: everything except main().
add_library(cramberry_auth STATIC
  backend/data-api/Database.cpp
  backend/user/Validation.cpp
  backend/user/Crypto.cpp
  backend/user/UserRepository.cpp
  backend/user/AuthService.cpp
  backend/user/UserController.cpp
)
target_include_directories(cramberry_auth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend)
target_link_libraries(cramberry_auth PUBLIC
  SQLite::SQLite3
  unofficial-sodium::sodium
  httplib::httplib
  nlohmann_json::nlohmann_json
)
target_compile_definitions(cramberry_auth PUBLIC $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>)
if(MSVC)
  target_compile_options(cramberry_auth PRIVATE /W4 /utf-8)
else()
  target_compile_options(cramberry_auth PRIVATE -Wall -Wextra)
endif()

if(CRAMBERRY_AUTH_BUILD_SERVER)
  add_executable(cramberry_server backend/main.cpp)
  target_link_libraries(cramberry_server PRIVATE cramberry_auth)
  target_compile_definitions(cramberry_server PRIVATE
    CRAMBERRY_FRONTEND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/frontend")
endif()

if(CRAMBERRY_AUTH_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)
  add_executable(auth_tests tests/test_auth.cpp tests/test_storage.cpp)
  target_link_libraries(auth_tests PRIVATE cramberry_auth GTest::gtest_main)
  gtest_discover_tests(auth_tests)
endif()
