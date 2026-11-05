# BASH! core — plain C++17, no JUCE. Shared by every plugin format, the iPad app and the tests.
add_library(bash_core STATIC
    src/MachineSynth.cpp
    src/Engine.cpp
    src/Scale.cpp
    src/Sequencer.cpp
    src/Slices.cpp)

target_include_directories(bash_core PUBLIC include)
target_compile_features(bash_core PUBLIC cxx_std_17)
set_target_properties(bash_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
