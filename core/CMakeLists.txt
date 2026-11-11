# BASH! core — plain C++17, no JUCE. Shared by every plugin format, the iPad app and the tests.
add_library(bash_core STATIC
    src/Drums.cpp
    src/MachineSynth.cpp
    src/Modulation.cpp
    src/Engine.cpp
    src/AudioFile.cpp
    src/Instrument.cpp
    src/Fx.cpp
    src/Scale.cpp
    src/Sequencer.cpp
    src/Slices.cpp
    src/StepSeq.cpp
    src/Warp.cpp
    src/Library.cpp
    src/LibraryLoops.cpp
    src/Grooves.cpp)

target_include_directories(bash_core PUBLIC include)
target_compile_features(bash_core PUBLIC cxx_std_17)
set_target_properties(bash_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# the version (from the top CMakeLists), so a device can tell when Live is still running an older engine
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt BASH_PROJECT_LINE REGEX "project\\(BASH VERSION")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" BASH_VERSION_FOUND "${BASH_PROJECT_LINE}")
target_compile_definitions(bash_core PUBLIC BASH_VERSION_STRING="${BASH_VERSION_FOUND}")
