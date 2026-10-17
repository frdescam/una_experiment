# Host tests for the ButtonLock core logic (no watch, no SDK runtime needed).
#
#   cmake -S tests -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#   build-tests/jacket_report        # false-unlock / accidental-start table
cmake_minimum_required(VERSION 3.21)
project(ButtonLockHostTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUTTONLOCK_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../apps/ButtonLock/Software/Libs/Core)

# A system GoogleTest (libgtest-dev) if there is one, else the same commit the
# UNA SDK's own host tests fetch.
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_library(buttonlock_core STATIC
    ${BUTTONLOCK_CORE_DIR}/ButtonLock/Sequence.cpp
    ${BUTTONLOCK_CORE_DIR}/ButtonLock/UnlockDetector.cpp
    ${BUTTONLOCK_CORE_DIR}/ButtonLock/LockController.cpp
    ${BUTTONLOCK_CORE_DIR}/ButtonLock/DrainWatchdog.cpp
)
target_include_directories(buttonlock_core PUBLIC ${BUTTONLOCK_CORE_DIR})
if(MSVC)
    target_compile_options(buttonlock_core PRIVATE /W4)
else()
    target_compile_options(buttonlock_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
endif()

add_executable(buttonlock_tests
    Sequence_test.cpp
    UnlockDetector_test.cpp
    LockController_test.cpp
    DrainWatchdog_test.cpp
    JacketModel_test.cpp
)
target_include_directories(buttonlock_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(buttonlock_tests PRIVATE buttonlock_core GTest::gtest_main)

add_executable(jacket_report jacket_report.cpp)
target_include_directories(jacket_report PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jacket_report PRIVATE buttonlock_core)

enable_testing()
include(GoogleTest)
gtest_discover_tests(buttonlock_tests)

# The unlock-sequence study (prints a Markdown report): seq_study [hostile-h] [calibrated-h] [threads] [seed]
find_package(Threads REQUIRED)
add_executable(seq_study seq_study.cpp)
target_include_directories(seq_study PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(seq_study PRIVATE buttonlock_core Threads::Threads)
