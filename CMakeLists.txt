cmake_minimum_required(VERSION 3.22)

project(PaulasRevenge VERSION 1.0.0 LANGUAGES C CXX)

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

# HELIX (Jamie's Twisted-Helix synth engine, vendored in third_party/helix-core)
file(GLOB PRV_HELIX_SRC CONFIGURE_DEPENDS third_party/helix-core/src/*.cpp)
add_library(helix_core STATIC ${PRV_HELIX_SRC})
target_include_directories(helix_core PUBLIC third_party/helix-core/include)
set_target_properties(helix_core PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(helix_core PRIVATE _USE_MATH_DEFINES NOMINMAX)
    target_compile_options(helix_core PRIVATE /W0 /bigobj)
else()
    target_compile_options(helix_core PRIVATE -w)
endif()

# CRAFTY (Jamie's Crafty Synth engine, vendored in third_party/crafty)
add_library(crafty_core STATIC
    third_party/crafty/core/cr_patch.c
    third_party/crafty/core/cr_arp.c
    third_party/crafty/core/cr_text.c
    third_party/crafty/dsp/cr_filter.c
    third_party/crafty/dsp/cr_fx.c
    third_party/crafty/dsp/cr_synth.c
    third_party/crafty/dsp/cr_wt.c
    third_party/crafty/host/cr_inst.c
    third_party/crafty/host/cr_proto.c
    third_party/crafty/host/cr_wav.c
    third_party/crafty/host/cr_tables.c)
target_include_directories(crafty_core PUBLIC
    third_party/crafty/core third_party/crafty/dsp third_party/crafty/host third_party/crafty/gen)
set_target_properties(crafty_core PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(crafty_core PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
    target_compile_options(crafty_core PRIVATE /W0)
else()
    target_compile_options(crafty_core PRIVATE -w)
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
    Source/engine/Tg101Synth.cpp
    Source/engine/HelixSynth.cpp
    Source/engine/CraftySynth.cpp
    Source/engine/DrumSynth.cpp
    Source/engine/MegaFilter.cpp
    Source/engine/mega/Filter.cpp
    Source/engine/PadSynth.cpp
    Source/engine/LiveFx.cpp
    Source/engine/Crunch.cpp
    Source/engine/LowEnd.cpp
    Source/engine/GearMaps.cpp
    Source/engine/AkaiSysex.cpp)
target_include_directories(prv_engine PUBLIC Source)
target_link_libraries(prv_engine PRIVATE helix_core crafty_core)
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
    # (v1.00) ONNX Runtime: GitHub's release first; if that fails (its downloads went 404 for a while), the same
    # version from NuGet, laid out the same way (include/, lib/, LICENSE). Kept in _deps/onnxruntime-src (cached in CI)
    set(ORT_DIR ${CMAKE_BINARY_DIR}/_deps/onnxruntime-src)
    if(NOT EXISTS ${ORT_DIR}/include/onnxruntime_cxx_api.h)
        set(ORT_TMP ${CMAKE_BINARY_DIR}/_deps/onnxruntime-tmp)
        file(REMOVE_RECURSE ${ORT_TMP} ${ORT_DIR})
        file(MAKE_DIRECTORY ${ORT_TMP})
        get_filename_component(ORT_ARCHIVE_NAME ${ORT_URL} NAME)
        set(ORT_OK FALSE)
        foreach(ORT_TRY RANGE 1 3)
            file(DOWNLOAD ${ORT_URL} ${ORT_TMP}/${ORT_ARCHIVE_NAME} STATUS ORT_ST)
            list(GET ORT_ST 0 ORT_CODE)
            if(ORT_CODE EQUAL 0)
                set(ORT_OK TRUE)
                break()
            endif()
            message(STATUS "ONNX Runtime from GitHub: try ${ORT_TRY} failed (${ORT_ST})")
        endforeach()
        if(ORT_OK)
            file(ARCHIVE_EXTRACT INPUT ${ORT_TMP}/${ORT_ARCHIVE_NAME} DESTINATION ${ORT_TMP}/x)
            file(GLOB ORT_TOP LIST_DIRECTORIES true ${ORT_TMP}/x/onnxruntime-*)
            file(RENAME ${ORT_TOP} ${ORT_DIR})
        else()
            message(STATUS "ONNX Runtime: from NuGet instead")
            set(ORT_NUPKG https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg)
            file(DOWNLOAD ${ORT_NUPKG} ${ORT_TMP}/ort.zip STATUS ORT_ST)
            list(GET ORT_ST 0 ORT_CODE)
            if(NOT ORT_CODE EQUAL 0)
                message(FATAL_ERROR "ONNX Runtime ${ORT_VERSION}: neither GitHub nor NuGet would download it (${ORT_ST}) - or build with -DPRV_STEMS=OFF")
            endif()
            file(ARCHIVE_EXTRACT INPUT ${ORT_TMP}/ort.zip DESTINATION ${ORT_TMP}/n)
            set(N ${ORT_TMP}/n)
            file(MAKE_DIRECTORY ${ORT_DIR}/lib)
            file(COPY ${N}/build/native/include/ DESTINATION ${ORT_DIR}/include)
            file(GLOB ORT_LIC ${N}/LICENSE*)
            if(ORT_LIC)
                list(GET ORT_LIC 0 ORT_LIC1)
                file(COPY_FILE ${ORT_LIC1} ${ORT_DIR}/LICENSE)
            endif()
            if(APPLE)
                # one universal dylib from the two, named and identified as the GitHub one is
                execute_process(COMMAND lipo -create ${N}/runtimes/osx-arm64/native/libonnxruntime.dylib ${N}/runtimes/osx-x64/native/libonnxruntime.dylib
                                        -output ${ORT_DIR}/lib/libonnxruntime.${ORT_VERSION}.dylib RESULT_VARIABLE ORT_R)
                if(NOT ORT_R EQUAL 0)
                    message(FATAL_ERROR "ONNX Runtime: lipo failed")
                endif()
                execute_process(COMMAND install_name_tool -id @rpath/libonnxruntime.${ORT_VERSION}.dylib ${ORT_DIR}/lib/libonnxruntime.${ORT_VERSION}.dylib)
            elseif(WIN32)
                file(COPY ${N}/runtimes/win-x64/native/onnxruntime.dll ${N}/runtimes/win-x64/native/onnxruntime.lib DESTINATION ${ORT_DIR}/lib)
            else()
                file(COPY_FILE ${N}/runtimes/linux-x64/native/libonnxruntime.so ${ORT_DIR}/lib/libonnxruntime.so.${ORT_VERSION})
            endif()
        endif()
        file(REMOVE_RECURSE ${ORT_TMP})
    endif()
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
    Source/ui/TablePage.cpp
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

# ASIO (Windows, v0.89): with -DPRV_ASIO_SDK=<the ASIO SDK folder> (the build fetches it from Steinberg; see build.yml),
# the app can play through an interface's ASIO driver - straight to the hardware, past Windows' sound processing.
# ASIO is a trademark and software of Steinberg Media Technologies GmbH (used under Steinberg's ASIO licence agreement)
set(PRV_ASIO_SDK "" CACHE PATH "The Steinberg ASIO SDK folder (Windows): builds in ASIO")
if(WIN32 AND PRV_ASIO_SDK AND EXISTS "${PRV_ASIO_SDK}/common/iasiodrv.h")
    message(STATUS "ASIO: on (${PRV_ASIO_SDK})")
    target_compile_definitions(PaulasRevenge PUBLIC JUCE_ASIO=1)
    target_include_directories(PaulasRevenge PRIVATE "${PRV_ASIO_SDK}/common")
elseif(WIN32)
    message(STATUS "ASIO: off (no PRV_ASIO_SDK)")
endif()

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
