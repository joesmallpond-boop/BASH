cmake_minimum_required(VERSION 3.22)

# BASH! for Max for Live: the core (shared C++), its tests and the tools the device build uses.
# Universal (Apple silicon + Intel) Mac builds, macOS 11+
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(BASH VERSION 1.2.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BASH_BUILD_TESTS  "Build the core unit tests" ON)
# The Max for Live device (the bash~ external) is built from max/ - see scripts/build-mac.sh

add_subdirectory(core)

if(BASH_BUILD_TESTS)
    enable_testing()
    add_executable(bash_core_tests tests/CoreTests.cpp)
    target_link_libraries(bash_core_tests PRIVATE bash_core)
    add_test(NAME core COMMAND bash_core_tests)
endif()

# the parameter table as JSON, for the Max for Live device generator (max/make_device.py)
add_executable(bash_params_json tools/ParamsJson.cpp)
target_link_libraries(bash_params_json PRIVATE bash_core)
add_executable(bash_device_sim tools/DeviceSim.cpp)
target_link_libraries(bash_device_sim PRIVATE bash_core)
