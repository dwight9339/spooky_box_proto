include_guard(GLOBAL)

set(ST_MULTICONTEXT DUAL_CORE CACHE STRING "Type of multi-context")
set(SPOOKY_FIRMWARE_BINARY_ROOT "${CMAKE_BINARY_DIR}/firmware")
set(SPOOKY_EXTERNAL_PREFIX_ROOT "${CMAKE_BINARY_DIR}/_external")
set(SPOOKY_FIRMWARE_SUFFIX ".elf")
option(SPOOKY_IPC_SMOKE "Build the unvalidated dual-core IPC bench experiment" OFF)
set(SPOOKY_IPC_M4_VERSION "1" CACHE STRING "M4 diagnostic ABI; 2 tests mismatch")
option(SPOOKY_RADIO_TUNE_QUALIFICATION
    "Allow in-band tuning while recording for bench qualification (54w.6)" OFF)
option(SPOOKY_DEMO
    "Build the Halloween 2026 demo-only behavior (decision 0011, demo branch only)" OFF)
option(SPOOKY_LOGGER_LOAD_QUALIFICATION
    "Add LOG LOAD and LOG FAULT for logger saturation bench qualification (jjy.6)" OFF)
option(SPOOKY_ISR_TIMING_QUALIFICATION
    "Time every M7 interrupt handler through a RAM vector table (jjy.11)" OFF)
option(SPOOKY_MATRIX_RECORDING_QUALIFICATION
    "Keep the decision 0013 matrix running during capture for bench qualification (54w.8)" OFF)
option(SPOOKY_SPEAKER_MONITOR
    "Monitor on the line-out speaker amplifier unless headphones are in (jr0)" OFF)
set(SPOOKY_RECORDING_CARD_RESERVE_SECONDS "60" CACHE STRING
    "Seconds of three-channel audio retained before reporting card full")
set(SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES "0" CACHE STRING
    "Bytes retained for one rolling-capture save; hpq.2 sets the window")
set(SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES "0" CACHE STRING
    "Additional allocation bytes needed to finalize a recording")
set(SPOOKY_RECORDING_WAV_MAX_FRAMES "715827876" CACHE STRING
    "Maximum WAV frames; override only for bounded storage-limit bench tests")
set(SPOOKY_RECORDING_PREALLOC_SECONDS "60" CACHE STRING
    "Seconds of audio preallocated contiguously before capture starts (jjy.9)")
set(SPOOKY_RECORD_PREPARE_STEP_MS "32" CACHE STRING
    "Foreground milliseconds per recording preparation step (jjy.9)")
set(SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS "16" CACHE STRING
    "Longest used FAT stretch, in FAT sectors, a recording may grow across (jjy.17)")
set(SPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS "4" CACHE STRING
    "Audio queue high-water (of 8 blocks) that reports low storage margin (decision 0014)")
set(SPOOKY_STORAGE_MARGIN_WRITE_MS "341" CACHE STRING
    "Single SD write duration that reports low storage margin (decision 0014)")
set(SPOOKY_BUILD_ID "unidentified" CACHE STRING
    "ASCII build identity reported by the M7 target")
string(LENGTH "${SPOOKY_BUILD_ID}" SPOOKY_BUILD_ID_LENGTH)
if(SPOOKY_BUILD_ID_LENGTH GREATER 128 OR
   NOT SPOOKY_BUILD_ID MATCHES "^[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "SPOOKY_BUILD_ID must be 1..128 ASCII token characters")
endif()

function(spooky_add_core core_name target_name)
    set(core_source_dir "${PROJECT_SOURCE_DIR}/${core_name}")
    set(core_binary_dir "${SPOOKY_FIRMWARE_BINARY_ROOT}/${core_name}")
    set(core_identity_arg)
    if(core_name STREQUAL "CM7")
        list(APPEND core_identity_arg
            "-DSPOOKY_BUILD_ID:STRING=${SPOOKY_BUILD_ID}")
    endif()

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
            "-DSPOOKY_IPC_SMOKE:BOOL=${SPOOKY_IPC_SMOKE}"
            "-DSPOOKY_IPC_M4_VERSION:STRING=${SPOOKY_IPC_M4_VERSION}"
            "-DSPOOKY_RADIO_TUNE_QUALIFICATION:BOOL=${SPOOKY_RADIO_TUNE_QUALIFICATION}"
            "-DSPOOKY_DEMO:BOOL=${SPOOKY_DEMO}"
            "-DSPOOKY_LOGGER_LOAD_QUALIFICATION:BOOL=${SPOOKY_LOGGER_LOAD_QUALIFICATION}"
            "-DSPOOKY_ISR_TIMING_QUALIFICATION:BOOL=${SPOOKY_ISR_TIMING_QUALIFICATION}"
            "-DSPOOKY_MATRIX_RECORDING_QUALIFICATION:BOOL=${SPOOKY_MATRIX_RECORDING_QUALIFICATION}"
            "-DSPOOKY_SPEAKER_MONITOR:BOOL=${SPOOKY_SPEAKER_MONITOR}"
            "-DSPOOKY_RECORDING_CARD_RESERVE_SECONDS:STRING=${SPOOKY_RECORDING_CARD_RESERVE_SECONDS}"
            "-DSPOOKY_ROLLING_CAPTURE_RESERVE_BYTES:STRING=${SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES}"
            "-DSPOOKY_RECORDING_FINALIZE_RESERVE_BYTES:STRING=${SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES}"
            "-DSPOOKY_RECORDING_WAV_MAX_FRAMES:STRING=${SPOOKY_RECORDING_WAV_MAX_FRAMES}"
            "-DSPOOKY_RECORDING_PREALLOC_SECONDS:STRING=${SPOOKY_RECORDING_PREALLOC_SECONDS}"
            "-DSPOOKY_RECORD_PREPARE_STEP_MS:STRING=${SPOOKY_RECORD_PREPARE_STEP_MS}"
            "-DSPOOKY_RECORDING_MAX_GAP_FAT_SECTORS:STRING=${SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS}"
            "-DSPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS:STRING=${SPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS}"
            "-DSPOOKY_STORAGE_MARGIN_WRITE_MS:STRING=${SPOOKY_STORAGE_MARGIN_WRITE_MS}"
            ${core_identity_arg}
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
