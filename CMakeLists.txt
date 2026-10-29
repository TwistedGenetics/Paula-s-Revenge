cmake_minimum_required(VERSION 3.22)

project(PaulasRevenge VERSION 0.25.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(PRV_BUILD_TESTS "Build the engine test program" ON)

# JUCE: use a local checkout if given (-DJUCE_DIR=...), otherwise fetch it
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Plain C++ tracker engine (no JUCE) - shared by the plugin and the tests
add_library(prv_engine STATIC
    Source/engine/Player.cpp
    Source/engine/ModuleIO.cpp
    Source/engine/SongOps.cpp
    Source/engine/Commands.cpp
    Source/engine/TimeStretch.cpp
    Source/engine/Slicer.cpp
    Source/engine/MixerTypes.cpp
    Source/engine/Effects.cpp
    Source/engine/Mixer.cpp
    Source/engine/PluginSynth.cpp
    Source/engine/MidiOut.cpp
    Source/engine/Bounce.cpp
    Source/engine/Chords.cpp
    Source/engine/Blep.cpp
    Source/engine/Capture.cpp
    Source/engine/ChordDetect.cpp
    Source/engine/Recorder.cpp
    Source/engine/SampleTools.cpp
    Source/engine/CvOut.cpp
    Source/engine/Utility.cpp
    Source/engine/ChordDatabase.cpp)
target_include_directories(prv_engine PUBLIC Source)
set_target_properties(prv_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# v0.8: the standalone app only (no plug-in builds)
set(PRV_FORMATS Standalone)

juce_add_plugin(PaulasRevenge
    COMPANY_NAME "Twisted Genetics"
    BUNDLE_ID "com.twistedgenetics.paulasrevenge"
    PRODUCT_NAME "Paula's Revenge"
    PLUGIN_MANUFACTURER_CODE Twgn
    PLUGIN_CODE PlRv
    FORMATS ${PRV_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Paula's Revenge records audio into instruments on its SAMPLE page."
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_32.png")

juce_generate_juce_header(PaulasRevenge)

target_sources(PaulasRevenge PRIVATE
    Source/PluginProcessor.cpp
    Source/History.cpp
    Source/PluginEditor.cpp
    Source/SongFile.cpp
    Source/ui/PixelFont.cpp
    Source/ui/Widgets.cpp
    Source/ui/PatternEditor.cpp
    Source/ui/Panels.cpp
    Source/ui/Spectrum.cpp
    Source/ui/SampleEditor.cpp
    Source/ui/SampleBrowser.cpp
    Source/ui/ParamsPage.cpp
    Source/ui/ArrangePage.cpp
    Source/ui/MixerPage.cpp
    Source/ui/SeqEditor.cpp
    Source/ui/CommandPicker.cpp
    Source/ui/SynthPage.cpp
    Source/ui/MidiOutPage.cpp
    Source/ui/HelpPage.cpp
    Source/ui/ChordsPage.cpp
    Source/ui/ChordPicker.cpp
    Source/ui/SamplePage.cpp
    Source/ui/CvPage.cpp
    Source/ui/EqGraph.cpp
    Source/OutputFollower.cpp
    Source/PluginHost.cpp
    Source/MidiOutHost.cpp
    Source/StandaloneApp.cpp)

if(APPLE)
    target_sources(PaulasRevenge PRIVATE Source/mac/FullScreen.mm Source/mac/DefaultOutput.cpp)
elseif(WIN32)
    target_sources(PaulasRevenge PRIVATE Source/win/DefaultOutput.cpp)
    target_link_libraries(PaulasRevenge PRIVATE ole32)
endif()

target_compile_definitions(PaulasRevenge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<IF:$<PLATFORM_ID:Darwin>,1,0>
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1)

# the README and the PDF manual, built in (the HELP page)
juce_add_binary_data(PaulasRevengeDocs
    NAMESPACE PrvDocs
    SOURCES README.md "docs/manual/Paulas Revenge Manual.pdf")

target_link_libraries(PaulasRevenge
    PRIVATE
        prv_engine
        PaulasRevengeDocs
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PRV_BUILD_TESTS)
    add_executable(prv_engine_test Tests/EngineTest.cpp)
    target_link_libraries(prv_engine_test PRIVATE prv_engine)
    if (MSVC)
        # the tests keep Players (~470 KB each) on the stack; Windows gives 1 MB by default
        target_link_options(prv_engine_test PRIVATE /STACK:16777216)
    endif()
endif()
