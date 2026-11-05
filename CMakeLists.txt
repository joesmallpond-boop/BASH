cmake_minimum_required(VERSION 3.22)

# Universal (Apple silicon + Intel) Mac builds, macOS 11+
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(BASH VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BASH_BUILD_PLUGIN "Build the JUCE plugin and standalone app" ON)
option(BASH_BUILD_TESTS  "Build the core unit tests" ON)

add_subdirectory(core)

if(BASH_BUILD_TESTS)
    enable_testing()
    add_executable(bash_core_tests tests/CoreTests.cpp)
    target_link_libraries(bash_core_tests PRIVATE bash_core)
    add_test(NAME core COMMAND bash_core_tests)
endif()

if(BASH_BUILD_PLUGIN)
    include(FetchContent)
    # To build offline, pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.10
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()
