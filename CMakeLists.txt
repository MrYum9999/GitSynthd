cmake_minimum_required(VERSION 3.22)

# macOS settings must come before project()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary on macOS")

project(GitSynthd VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.5.zip)
FetchContent_MakeAvailable(JUCE)

# ---- Formats ---------------------------------------------------------------
# VST3 + Standalone everywhere, AU on macOS. AAX is added automatically when
# you point CMake at Avid's AAX SDK:  cmake -B build -DAAX_SDK_PATH=C:/SDKs/aax-sdk
set(GITSYNTHD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GITSYNTHD_FORMATS AU)
endif()
if(AAX_SDK_PATH)
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND GITSYNTHD_FORMATS AAX)
endif()

juce_add_plugin(GitSynthd
    COMPANY_NAME                "Will"
    PLUGIN_MANUFACTURER_CODE    Will
    PLUGIN_CODE                 Gsyn
    PLUGIN_NAME                 "GitSynth'd!"
    PRODUCT_NAME                "GitSynthd"
    FORMATS                     ${GITSYNTHD_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Distortion Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_Harmonic)

target_sources(GitSynthd PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(GitSynthd PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(GitSynthd
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Optional offline DSP tests (not needed to build the plugin) -----------
option(GITSYNTHD_TESTS "Build the offline DSP test program" OFF)
if(GITSYNTHD_TESTS)
    juce_add_console_app(GitSynthdTests PRODUCT_NAME "GitSynthdTests")
    target_sources(GitSynthdTests PRIVATE Tests/DspTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(GitSynthdTests PRIVATE Source)
    target_compile_definitions(GitSynthdTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(GitSynthdTests PRIVATE juce::juce_audio_utils juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)
endif()
