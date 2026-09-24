# Included by both maintained core projects, never by CubeMX source lists.
option(SPOOKY_IPC_SMOKE "Build the unvalidated dual-core IPC bench experiment" OFF)
set(SPOOKY_IPC_M4_VERSION "1" CACHE STRING "M4 diagnostic ABI; 2 tests mismatch")
if(NOT SPOOKY_IPC_M4_VERSION MATCHES "^[12]$")
    message(FATAL_ERROR "SPOOKY_IPC_M4_VERSION must be 1 (normal) or 2 (mismatch)")
endif()
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/${STM32_LINKER_SCRIPT}")
if(SPOOKY_IPC_SMOKE)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SPOOKY_IPC_SMOKE=1)
    if(CMAKE_PROJECT_NAME MATCHES "CM4$")
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
            IPC_SMOKE_VERSION=${SPOOKY_IPC_M4_VERSION}U)
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../Common/Src/ipc_smoke.c
        ${CMAKE_CURRENT_LIST_DIR}/../Common/Src/ipc_smoke_protocol.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../Common/Inc)
endif()
