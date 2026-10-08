# RideLock LVGL PC simulator: the app's service and LVGL GUI processes running
# against the SDK's mock kernel, with the display in an SDL2 window.
#
# Linux or WSL (needs GCC, CMake, Ninja and libsdl2-dev):
#   export UNA_SDK=/path/to/una-sdk
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   (cd build/bin && ./RideLockSimulator)
#
# Keys 1..4 are L1, L2, R1, R2; Esc closes. Run from build/bin so the mock file
# system lands in ../../../../../Output (Software/Output, ignored by git); put
# an app_config.json there to try other settings.
#
# Headless and scripted (see SimScript.hpp):
#   SDL_VIDEODRIVER=dummy RIDELOCK_SIM_SCRIPT=../../scripts/unlock.txt ./RideLockSimulator
cmake_minimum_required(VERSION 3.21)

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Point it at the SDK root.")
endif()

project(RideLockSimulator C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Source lists: the mock kernel, the LVGL port and host, the app's own code.
include($ENV{UNA_SDK}/cmake/una-simulator.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../Libs/libs.cmake)   # LIBS_SOURCES, LIBS_INCLUDE_DIRS, RIDELOCK_CORE_SOURCES
include(${CMAKE_CURRENT_SOURCE_DIR}/../lvgl-gui.cmake)          # GUI_APP_SOURCES, GUI_APP_INCLUDE_DIRS

# The core is in both lists (service and GUI); one process here, so link it once.
list(REMOVE_DUPLICATES GUI_APP_SOURCES)
list(REMOVE_ITEM GUI_APP_SOURCES ${RIDELOCK_CORE_SOURCES})

# One output directory for every configuration, so the relative mock file
# system path resolves the same everywhere.
set(SIM_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SIM_BIN_DIR}")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${SIM_BIN_DIR}")
endforeach()

add_executable(RideLockSimulator
    main.cpp
    SimScript.cpp
    ${GUI_APP_SOURCES}
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_SIMULATOR}
    ${UNA_SDK_SOURCES_SIMULATOR_SERVICE}
    ${UNA_SDK_SOURCES_SIMULATOR_LVGL}
)

target_include_directories(RideLockSimulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # ConfigurationSimulator.hpp
    ${GUI_APP_INCLUDE_DIRS}
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_SIMULATOR}
    ${UNA_SDK_INCLUDE_DIRS_GUI_LVGL}
)

# The identity the .uapp build derives from CMakeLists.txt.
target_compile_definitions(RideLockSimulator PRIVATE
    ${UNA_SDK_DEFINES_SIMULATOR}
    ${UNA_SDK_DEFINES_GUI_LVGL}
    APP_ID="5CBC53B94083EF7D"
    DEV_ID="UNA Simulator"
    BUILD_VERSION="0.1.0-sim"
)

find_package(Threads REQUIRED)
target_link_libraries(RideLockSimulator PRIVATE Threads::Threads)
una_simulator_link_sdl2(RideLockSimulator)

if(MSVC)
    target_compile_options(RideLockSimulator PRIVATE /W3 /MP)
else()
    target_compile_options(RideLockSimulator PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# The service on its own, with the harness playing kernel and GUI: covers what
# one simulated launch cannot (lock at boot, relock). See ServiceHarness.cpp.
#   build/bin/RideLockServiceHarness relock /tmp/ridelock-harness
add_executable(RideLockServiceHarness
    ServiceHarness.cpp
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_SIMULATOR}
    ${UNA_SDK_SOURCES_SIMULATOR_SERVICE}
)
target_include_directories(RideLockServiceHarness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GUI_APP_INCLUDE_DIRS}              # the SDK's sensor layer finds ../../simulator/ConfigurationSimulator.hpp through it
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_SIMULATOR}
)
target_compile_definitions(RideLockServiceHarness PRIVATE
    ${UNA_SDK_DEFINES_SIMULATOR}
    APP_ID="5CBC53B94083EF7D"
    DEV_ID="UNA Simulator"
    BUILD_VERSION="0.1.0-sim"
)
target_link_libraries(RideLockServiceHarness PRIVATE Threads::Threads)
if(NOT MSVC)
    # Warnings for the harness itself, not for the SDK sources it links.
    set_source_files_properties(ServiceHarness.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()
