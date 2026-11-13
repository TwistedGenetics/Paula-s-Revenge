cmake_minimum_required(VERSION 3.22)

project(PaulasRevenge VERSION 0.82.0 LANGUAGES C CXX)

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
    Source/engine/StudioFx.cpp
    Source/engine/StudioFx2.cpp
    Source/engine/Modulation.cpp
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
    Source/engine/Groove.cpp
    Source/engine/BreakTools.cpp
    Source/engine/StretchSync.cpp
    Source/engine/Radio.cpp
    Source/engine/RadioFormats.cpp
    Source/engine/ChordDatabase.cpp
    Source/engine/BuiltinSynths.cpp
    Source/engine/ReeseSynth.cpp
    Source/engine/RaveSynth.cpp
    Source/engine/RomSynth.cpp
    Source/engine/RomBank.cpp
    Source/engine/MorphFilter.cpp
    Source/engine/MorphSynth.cpp
    Source/engine/AcidSynth.cpp
    Source/engine/PadSynth.cpp
    Source/engine/LiveFx.cpp
    Source/engine/Crunch.cpp
    Source/engine/LowEnd.cpp
    Source/engine/GearMaps.cpp
    Source/engine/AkaiSysex.cpp)
target_include_directories(prv_engine PUBLIC Source)
# STEMS: Demucs v4 in ONNX Runtime (Microsoft's prebuilt library, downloaded here; shipped with the app)
if(APPLE OR WIN32)
    option(PRV_STEMS "Build STEMS (ONNX Runtime)" ON)
else()
    option(PRV_STEMS "Build STEMS (ONNX Runtime)" OFF)
endif()
target_sources(prv_engine PRIVATE Source/engine/stems/Stems.cpp)
if(PRV_STEMS)
    set(ORT_VERSION 1.20.1)
    if(APPLE)
        set(ORT_URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-universal2-${ORT_VERSION}.tgz)
    elseif(WIN32)
        set(ORT_URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip)
    else()
        set(ORT_URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz)
    endif()
    include(FetchContent)
    FetchContent_Declare(onnxruntime URL ${ORT_URL} SOURCE_SUBDIR none)
    FetchContent_MakeAvailable(onnxruntime)
    set(ORT_DIR ${onnxruntime_SOURCE_DIR})
    add_library(prv_onnxruntime SHARED IMPORTED GLOBAL)
    if(APPLE)
        set(ORT_LIB ${ORT_DIR}/lib/libonnxruntime.${ORT_VERSION}.dylib)
        set_target_properties(prv_onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_LIB})
    elseif(WIN32)
        set(ORT_LIB ${ORT_DIR}/lib/onnxruntime.dll)
        set_target_properties(prv_onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_LIB} IMPORTED_IMPLIB ${ORT_DIR}/lib/onnxruntime.lib)
    else()
        set(ORT_LIB ${ORT_DIR}/lib/libonnxruntime.so.${ORT_VERSION})
        set_target_properties(prv_onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_LIB} IMPORTED_SONAME libonnxruntime.so.1)
    endif()
    set_target_properties(prv_onnxruntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ORT_DIR}/include)
    target_sources(prv_engine PRIVATE Source/engine/stems/OnnxModel.cpp)
    target_link_libraries(prv_engine PUBLIC prv_onnxruntime)
    target_compile_definitions(prv_engine PUBLIC PRV_STEMS=1)
else()
    target_sources(prv_engine PRIVATE Source/engine/stems/OnnxNone.cpp)
endif()

# the internet radio's AAC: the computer's own decoder (none on Linux, where only the tests run)
if(APPLE)
    target_sources(prv_engine PRIVATE Source/engine/aac/AacMac.cpp)
    target_link_libraries(prv_engine PUBLIC "-framework AudioToolbox" "-framework CoreFoundation")
elseif(WIN32)
    target_sources(prv_engine PRIVATE Source/engine/aac/AacWin.cpp)
    target_link_libraries(prv_engine PUBLIC mfplat mfuuid wmcodecdspuuid ole32)
else()
    target_sources(prv_engine PRIVATE Source/engine/aac/AacNone.cpp)
endif()
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
    # INTERNET RADIO: many stations stream over plain http
    PLIST_TO_MERGE "<plist><dict><key>NSAppTransportSecurity</key><dict><key>NSAllowsArbitraryLoads</key><true/></dict></dict></plist>"
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
    Source/ui/LanePads.cpp
    Source/ui/PhraseView.cpp
    Source/ui/EnginePage.cpp
    Source/ui/LivePage.cpp
    Source/ui/VisualsPage.cpp
    Source/ui/Spectrum.cpp
    Source/ui/SampleEditor.cpp
    Source/ui/SampleBrowser.cpp
    Source/ui/ParamsPage.cpp
    Source/ui/ArrangePage.cpp
    Source/ui/MixerPage.cpp
    Source/ui/ModPage.cpp
    Source/ui/AutomationPanel.cpp
    Source/ui/SeqEditor.cpp
    Source/ui/CommandPicker.cpp
    Source/ui/SynthPage.cpp
    Source/ui/MidiOutPage.cpp
    Source/ui/AkaiPanel.cpp
    Source/ui/HelpPage.cpp
    Source/ui/ChordsPage.cpp
    Source/ui/ChordPicker.cpp
    Source/ui/CommandPalette.cpp
    Source/ui/BreakPage.cpp
    Source/ui/SamplePage.cpp
    Source/ui/CvPage.cpp
    Source/ui/EqGraph.cpp
    Source/ui/RadioPage.cpp
    Source/RadioHost.cpp
    Source/Diagnostics.cpp
    Source/StemsJob.cpp
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

# STEMS: ONNX Runtime goes with the app (inside the Mac bundle, next to the Windows .exe)
if(PRV_STEMS)
    if(APPLE)
        set_target_properties(PaulasRevenge_Standalone PROPERTIES BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
        add_custom_command(TARGET PaulasRevenge_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:PaulasRevenge_Standalone>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_LIB} "$<TARGET_BUNDLE_CONTENT_DIR:PaulasRevenge_Standalone>/Frameworks/"
            VERBATIM)   # (the ' in "Paula's Revenge" needs quoting)
    elseif(WIN32)
        add_custom_command(TARGET PaulasRevenge_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_LIB} "$<TARGET_FILE_DIR:PaulasRevenge_Standalone>"
            VERBATIM)
    endif()
endif()

# the README and the PDF manual, built in (the HELP page)
juce_add_binary_data(PaulasRevengeDocs
    NAMESPACE PrvDocs
    SOURCES README.md "docs/manual/Paulas Revenge Manual.pdf")

# the smooth-type skins' typefaces (all SIL Open Font License: Resources/fonts)
juce_add_binary_data(PaulasRevengeFonts
    HEADER_NAME PrvFonts.h
    NAMESPACE PrvFonts
    SOURCES Resources/fonts/JetBrainsMono-Bold.ttf Resources/fonts/Inter-SemiBold.ttf Resources/fonts/SpaceGrotesk-Bold.ttf
            Resources/fonts/BarlowCondensed-SemiBold.ttf Resources/fonts/Nunito-ExtraBold.ttf)

target_link_libraries(PaulasRevenge
    PRIVATE
        prv_engine
        PaulasRevengeDocs
        PaulasRevengeFonts
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PRV_BUILD_TESTS)
    add_executable(prv_engine_test Tests/EngineTest.cpp)
    target_link_libraries(prv_engine_test PRIVATE prv_engine)
    if(PRV_STEMS AND WIN32)
        add_custom_command(TARGET prv_engine_test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_LIB} "$<TARGET_FILE_DIR:prv_engine_test>" VERBATIM)
    endif()
    if (MSVC)
        # the tests keep Players (~470 KB each) on the stack; Windows gives 1 MB by default
        target_link_options(prv_engine_test PRIVATE /STACK:16777216)
    endif()
endif()
