include_guard(GLOBAL)

set(ST_MULTICONTEXT DUAL_CORE CACHE STRING "Type of multi-context")
set(SPOOKY_FIRMWARE_BINARY_ROOT "${CMAKE_BINARY_DIR}/firmware")
set(SPOOKY_EXTERNAL_PREFIX_ROOT "${CMAKE_BINARY_DIR}/_external")
set(SPOOKY_FIRMWARE_SUFFIX ".elf")

function(spooky_add_core core_name target_name)
    set(core_source_dir "${PROJECT_SOURCE_DIR}/${core_name}")
    set(core_binary_dir "${SPOOKY_FIRMWARE_BINARY_ROOT}/${core_name}")

    ExternalProject_Add(${target_name}
        SOURCE_DIR                  "${core_source_dir}"
        BINARY_DIR                  "${core_binary_dir}"
        PREFIX                      "${SPOOKY_EXTERNAL_PREFIX_ROOT}/${core_name}"
        CONFIGURE_HANDLED_BY_BUILD  true
        INSTALL_COMMAND             ""
        CMAKE_ARGS
            "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON"
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
            "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
        BUILD_ALWAYS                true
    )
endfunction()

if((${BUILD_CONTEXT} MATCHES .*CM4.*) OR (NOT DEFINED BUILD_CONTEXT))
    message("   Build context: CM4")
    spooky_add_core(CM4 full_spooky_proto_CM4)
    set(ST_DUAL_CORE_CM4_PROJECT_BUILD_TARGET
        "${SPOOKY_FIRMWARE_BINARY_ROOT}/CM4/full_spooky_proto_CM4${SPOOKY_FIRMWARE_SUFFIX}"
        CACHE FILEPATH "Path to CM4 project target" FORCE)
endif()

if((${BUILD_CONTEXT} MATCHES .*CM7.*) OR (NOT DEFINED BUILD_CONTEXT))
    message("   Build context: CM7")
    spooky_add_core(CM7 full_spooky_proto_CM7)
    set(ST_DUAL_CORE_CM7_PROJECT_BUILD_TARGET
        "${SPOOKY_FIRMWARE_BINARY_ROOT}/CM7/full_spooky_proto_CM7${SPOOKY_FIRMWARE_SUFFIX}"
        CACHE FILEPATH "Path to CM7 project target" FORCE)
endif()
