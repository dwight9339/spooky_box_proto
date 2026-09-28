cmake_minimum_required(VERSION 3.22)
project(spooky_host_tests LANGUAGES C)
enable_testing()
function(spooky_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE stubs ../Common/Inc ../CM7/App)
    set_target_properties(${name} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_compile_definitions(${name} PRIVATE SPOOKY_LOGGER_HOST_TEST)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX /wd4996)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -pedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()
# Adds StateSmith-generated machines from CM7/App/sm (decision 0006) to a test.
# Some generated handlers ignore their `sm` argument, and MSVC reports the code
# StateSmith emits after a transition's return as unreachable, so those two
# warnings are suppressed for these files only. Pass paths relative to CM7/App/sm.
function(spooky_generated_sm target)
    list(TRANSFORM ARGN PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/../CM7/App/sm/ OUTPUT_VARIABLE sources)
    target_sources(${target} PRIVATE ${sources})
    target_include_directories(${target} PRIVATE ../CM7/App/sm)
    if(MSVC)
        set(generated_options /wd4100 /wd4702)
    else()
        set(generated_options -Wno-unused-parameter)
    endif()
    set_source_files_properties(${sources} TARGET_DIRECTORY ${target}
        PROPERTIES COMPILE_OPTIONS "${generated_options}")
endfunction()
spooky_test(logger_diag_test logger_diag_test.c stubs/fake_hal.c
    ../Common/Src/log_buffer.c ../Common/Src/diag_history.c
    ../CM7/App/target_logger.c ../CM7/App/diagnostics.c
    ../CM7/App/event_queue.c ../CM7/App/app_events.c)
spooky_test(event_queue_test event_queue_test.c ../CM7/App/event_queue.c)
spooky_test(command_policy_test command_policy_test.c ../CM7/App/command_policy.c)
spooky_test(session_test session_test.c ../CM7/App/sm/session_port.c)
spooky_generated_sm(session_test SessionSm.c)
spooky_test(session_authority_test session_authority_test.c
    ../CM7/App/session_control.c ../CM7/App/app_dispatch.c ../CM7/App/app_events.c
    ../CM7/App/event_queue.c ../CM7/App/sm/session_port.c)
spooky_generated_sm(session_authority_test SessionSm.c)
target_include_directories(session_authority_test PRIVATE ../CM7/Core/Inc)
spooky_test(input_resolution_test input_resolution_test.c ../CM7/App/sm/input_resolution_port.c)
spooky_generated_sm(input_resolution_test InputResolutionSm.c)
add_executable(ipc_smoke_protocol_test
    ipc_smoke_protocol_test.c
    ../Common/Src/ipc_smoke_protocol.c)
target_include_directories(ipc_smoke_protocol_test PRIVATE ../Common/Inc)
set_target_properties(ipc_smoke_protocol_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
    target_compile_options(ipc_smoke_protocol_test PRIVATE /W4 /WX)
else()
    target_compile_options(ipc_smoke_protocol_test PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME ipc_smoke_protocol COMMAND ipc_smoke_protocol_test)
