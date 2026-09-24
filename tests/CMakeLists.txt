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
spooky_test(logger_diag_test logger_diag_test.c stubs/fake_hal.c
    ../Common/Src/log_buffer.c ../Common/Src/diag_history.c
    ../CM7/App/target_logger.c ../CM7/App/diagnostics.c)
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
