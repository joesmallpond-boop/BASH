# BASH! for Max for Live: the bash~ external (the instrument) built from the shared core.
#   cmake -S max -B build-max -DMAX_SDK_BASE=/path/to/max-sdk-base
cmake_minimum_required(VERSION 3.19)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(BASH_M4L LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT MAX_SDK_BASE)
    message(FATAL_ERROR "Pass -DMAX_SDK_BASE=/path/to/max-sdk-base")
endif()

set(BASH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_BINARY_DIR}/core)

set(C74_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
add_subdirectory(bash~)
