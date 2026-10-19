cmake_minimum_required(VERSION 3.22)

project(MiniLiveStrip VERSION 0.1.0)

# JUCE is downloaded automatically during the first "cmake" configure step
# (needs an internet connection). No manual download required.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MiniLiveStrip
    COMPANY_NAME "Minimize"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Yrnm
    PLUGIN_CODE Fbkl
    FORMATS VST3 Standalone
    PRODUCT_NAME "Mini Live Strip")

target_sources(MiniLiveStrip PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h)

target_compile_features(MiniLiveStrip PRIVATE cxx_std_17)

target_compile_definitions(MiniLiveStrip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MiniLiveStrip
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
