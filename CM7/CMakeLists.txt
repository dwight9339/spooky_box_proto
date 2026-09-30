cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Compiler options
set(STM32_MCU_FLAGS  "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard " )

#Linker options
set(STM32_LINKER_SCRIPT stm32h755xx_flash_CM7.ld)
set(STM32_LINKER_OPTION  )

# Set the project name
set(CMAKE_PROJECT_NAME full_spooky_proto_CM7)
project(${CMAKE_PROJECT_NAME})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Include mx-generated
include("mx-generated.cmake")


# In order to use CMake for cross-compiling
set(CMAKE_CXX_COMPILER_FORCED "true")

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

option(SPOOKY_RADIO_TUNE_QUALIFICATION
    "Allow in-band tuning while recording for bench qualification (54w.6)" OFF)
if(SPOOKY_RADIO_TUNE_QUALIFICATION)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SPOOKY_RADIO_TUNE_QUALIFICATION=1)
endif()

set(SPOOKY_RECORDING_CARD_RESERVE_SECONDS "60" CACHE STRING
    "Seconds of three-channel audio retained before reporting card full")
set(SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES "0" CACHE STRING
    "Bytes retained for one rolling-capture save; hpq.2 sets the window")
set(SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES "0" CACHE STRING
    "Additional allocation bytes needed to finalize a recording")
set(SPOOKY_RECORDING_WAV_MAX_FRAMES "715827876" CACHE STRING
    "Maximum WAV frames; override only for bounded storage-limit bench tests")
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    SPOOKY_RECORDING_CARD_RESERVE_SECONDS=${SPOOKY_RECORDING_CARD_RESERVE_SECONDS}
    SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES=${SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES}
    SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES=${SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES}
    SPOOKY_RECORDING_WAV_MAX_FRAMES=${SPOOKY_RECORDING_WAV_MAX_FRAMES}
)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${CMAKE_CURRENT_SOURCE_DIR}/App/sm
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_Device/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Src/log_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Src/diag_history.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/target_logger.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/diagnostics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/foreground_budget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/event_queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/app_events.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/app_dispatch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/command_policy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/session_control.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/recording_limits.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/storage_lease.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/storage_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/sm/session_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/sm/SessionSm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/sm/radio_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/sm/RadioSm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/board_diagnostics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/audio_path_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/codec_volume_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/radio_control_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/radio_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/prototype_power.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ipc_smoke_cli.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/fuel_gauge_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/magnetometer_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/radio_recorder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/sd_diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/sd_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ui_board_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/wav_transfer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_Device/Src/usb_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_Device/Src/usbd_conf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_Device/Src/usbd_desc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c
)

include("../cmake/ipc_smoke.cmake")

# Use the same FatFs R0.12c release as the passing sibling SDMMC tests.
set(STM32CUBE_H7_ROOT
    "$ENV{USERPROFILE}/STM32Cube/Repository/STM32Cube_FW_H7_V1.13.0"
    CACHE PATH "STM32CubeH7 V1.13.0 root")
set(FATFS_ROOT "${STM32CUBE_H7_ROOT}/Middlewares/Third_Party/FatFs/src")
if(NOT EXISTS "${FATFS_ROOT}/ff.c")
    message(FATAL_ERROR
        "FatFs R0.12c not found at ${FATFS_ROOT}; set STM32CUBE_H7_ROOT")
endif()
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${FATFS_ROOT}/ff.c
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${FATFS_ROOT}
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined libraries
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
