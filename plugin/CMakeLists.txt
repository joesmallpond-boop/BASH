if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(BASH_FORMATS AUv3 Standalone)
elseif(APPLE)
    set(BASH_FORMATS VST3 AU Standalone)
else()
    set(BASH_FORMATS VST3 Standalone)
endif()

# App icon: the word BASH! in amber on charcoal (design/icon). iOS masks its own corners.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(BASH_ICON "${CMAKE_SOURCE_DIR}/design/icon/bash-icon-ios-1024.png")
else()
    set(BASH_ICON "${CMAKE_SOURCE_DIR}/design/icon/bash-icon-1024.png")
endif()

juce_add_plugin(BASH
    ICON_BIG                "${BASH_ICON}"
    ICON_SMALL              "${CMAKE_SOURCE_DIR}/design/icon/bash-icon-256.png"
    PRODUCT_NAME            "BASH"
    PLUGIN_NAME             "BASH!"
    DESCRIPTION             "An all-in-one kalabash solution"
    COMPANY_NAME            "Kalabash"
    BUNDLE_ID               "com.kalabash.bash"
    PLUGIN_MANUFACTURER_CODE Klbs
    PLUGIN_CODE             Bash
    FORMATS                 ${BASH_FORMATS}
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       TRUE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Instrument Synth Sampler
    AU_MAIN_TYPE            kAudioUnitType_MusicDevice
    # iPad app + AUv3
    FILE_SHARING_ENABLED    TRUE
    DOCUMENT_BROWSER_ENABLED TRUE
    BACKGROUND_AUDIO_ENABLED TRUE
    REQUIRES_FULL_SCREEN    FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
                             UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight)

target_sources(BASH PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    SamplerPanel.cpp
    SynthPanel.cpp
    DrumPanel.cpp
    MixerPanel.cpp
    ModPanel.cpp
    StepPanel.cpp
    ExportUI.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(BASH PUBLIC JUCE_CONTENT_SHARING=1)
endif()

target_compile_definitions(BASH PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BASH
    PRIVATE
        bash_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
