# RideLock: a lock screen that stops a jacket cuff (or anything else) from
# pressing the watch's buttons into an activity. Service + LVGL GUI.
#
#   export UNA_SDK=/path/to/una-sdk          (see tools/bootstrap.sh)
#   cmake -G "Unix Makefiles" -S . -B build
#   cmake --build build
#
# The .uapp lands in build/ and in ../../../Output/.
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "RideLock")
set(APP_USER_NAME "RideLock")
set(APP_TYPE "Utility")
# The service starts with the watch, so a reboot (or a long press that powers
# the watch on inside a sleeve) comes up locked. See LockController.hpp.
set(APP_AUTOSTART On)
set(DEV_ID "UNA")
# md5("RideLock")[:16], per Docs/sdk-setup.md. Replace it with an ID from
# apps.unawatch.com before publishing.
set(APP_ID "5CBC53B94083EF7D")
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(GUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../LVGL-GUI")

# This repository carries no apps-v* tags for the SDK's version script.
if(NOT DEFINED BUILD_VERSION)
    set(BUILD_VERSION "0.1.0")
endif()

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Point it at the SDK root.")
endif()

# Includes all build steps and required tools
# Sets toolchain before project
include($ENV{UNA_SDK}/cmake/una-app.cmake)

# Includes sets: SCRIPTS_PATH,
# UNA_SDK_SOURCES_COMMON, UNA_SDK_SOURCES_SERVICE, UNA_SDK_SOURCES_GUI_LVGL,
# UNA_SDK_INCLUDE_DIRS_COMMON, UNA_SDK_INCLUDE_DIRS_SERVICE, UNA_SDK_INCLUDE_DIRS_GUI_LVGL,
# UNA_SDK_DEFINES_GUI_LVGL
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)

# Includes LIBS_SOURCES, LIBS_INCLUDE_DIRS, RIDELOCK_CORE_SOURCES
include(${LIBS_PATH}/libs.cmake)

# Includes GUI_APP_SOURCES, GUI_APP_INCLUDE_DIRS
include(${GUI_PATH}/lvgl-gui.cmake)

project(${APP_NAME})

# Set up version
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# Build service
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_SERVICE}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
)
una_app_build_service(${APP_NAME}Service.elf)

# Build GUI (LVGL)
set(GUI_SOURCES
    ${GUI_APP_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_GUI_LVGL}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_GUI_LVGL}
    ${GUI_APP_INCLUDE_DIRS}
)
set(GUI_COMPILE_DEFINITIONS
    ${UNA_SDK_DEFINES_GUI_LVGL}
)
# LVGL's software renderer recurses deeper than TouchGFX; give the GUI task
# more stack than the 10 KB default (as the SDK's LVGL tutorials do).
set(GUI_STACK_SIZE "24*1024")
una_app_build_gui(${APP_NAME}GUI.elf)

# Build the complete app using SDK functions
una_app_build_app()
