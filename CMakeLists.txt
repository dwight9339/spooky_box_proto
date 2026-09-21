cmake_minimum_required(VERSION 3.22)
include(ExternalProject)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Set the project name
set(CMAKE_PROJECT_NAME full_spooky_proto)

# The root project only orchestrates the two firmware projects. Compiler and
# language detection belongs to the CM4 and CM7 child builds.
project(${CMAKE_PROJECT_NAME} LANGUAGES NONE)

# The CubeMX-generated root mx-generated.cmake puts both configurations in
# CM4/build and CM7/build.  Keep dual-core orchestration in maintained code so
# Debug and Release outputs remain isolated under the selected preset tree.
include("cmake/dual_core_firmware.cmake")
