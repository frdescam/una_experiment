# Sources and include directories of the RideLock LVGL GUI process. The
# generated font C files under assets/ are committed, so no converter is needed
# to build. The GUI also links the pure RideLock core (the unlock detector),
# which libs.cmake lists as RIDELOCK_CORE_SOURCES.
file(GLOB_RECURSE GUI_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/gui/src/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/assets/fonts/*.c
)
list(APPEND GUI_APP_SOURCES ${RIDELOCK_CORE_SOURCES})

set(GUI_APP_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/gui/include
)
