# RideLock's shared code.
#
#   Core/     pure C++ with no SDK dependency: the unlock detector, the lock
#             policy and the drain watchdog. Linked by the service, the GUI
#             and the host tests (tests/ at the repository root).
#   Sources/  the service and its configuration table.
#   Header/   the service header and the service <-> GUI message contract.
file(GLOB_RECURSE RIDELOCK_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/Core/*.cpp
)

file(GLOB_RECURSE LIBS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/Sources/*.c
    ${CMAKE_CURRENT_LIST_DIR}/Sources/*.cpp
)
list(APPEND LIBS_SOURCES ${RIDELOCK_CORE_SOURCES})

set(LIBS_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
    ${CMAKE_CURRENT_LIST_DIR}/Core
)
