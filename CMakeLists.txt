cmake_minimum_required(VERSION 3.22)

project(HEMInstrumentForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(HF_BUILD_TESTS "Build the unit test console app" ON)
option(HF_BUILD_APP "Build the HEM Instrument Forge application" ON)
option(HF_BUILD_TOOLS "Build ModelRender (modeled instruments) and other tools" ON)

set(HEM_JUCE_DIR "" CACHE PATH "Optional local JUCE 8.0.15 checkout")
if(HEM_JUCE_DIR)
    add_subdirectory(${HEM_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Engine: no UI code.
set(HF_CORE_SOURCES
    Source/Core/SincResampler.cpp
    Source/Library/InstrumentDef.cpp
    Source/Library/Library.cpp
    Source/Sampler/LoadedInstrument.cpp
    Source/Sampler/DiskStreamer.cpp
    Source/Sampler/VoicePool.cpp
    Source/Stage/Stage.cpp
    Source/Session/Song.cpp
    Source/Engine/ForgeEngine.cpp
    Source/Engine/Exporter.cpp
    Source/Band/BandTypes.cpp
    Source/Band/BandComposer.cpp
    Source/Band/BandBridge.cpp
    Source/Master/ElitePlugin.cpp
    Source/Master/EliteMaster.cpp)

set(HF_MODEL_SOURCES
    Source/Models/BandInstruments.cpp
    Source/Models/ModelVoices.cpp
    Source/Models/ModelCatalog.cpp)

set(HF_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra)

set(HF_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_PLUGINHOST_VST3=0)

juce_add_binary_data(ForgeAssets
    NAMESPACE ForgeBinary
    HEADER_NAME ForgeBinaryData.h
    SOURCES
        Assets/Faceplates/Main-Faceplate.png
        Assets/Icon/HEM-Icon-1024.png
        Assets/Fonts/Poppins-Bold.ttf
        Assets/Fonts/Poppins-Medium.ttf)

set(HF_UI_SOURCES
    Source/UI/FaceplateKit.cpp
    Source/UI/ForgeLookAndFeel.cpp
    Source/UI/AppState.cpp
    Source/UI/MainComponent.cpp
    Source/UI/Pages/BandPage.cpp
    Source/UI/Pages/TracksPage.cpp
    Source/UI/Pages/PianoRoll.cpp
    Source/UI/Pages/StagePage.cpp
    Source/UI/Pages/InstrumentsPage.cpp
    Source/UI/Pages/ExportPanel.cpp)

if(HF_BUILD_APP)
    juce_add_gui_app(InstrumentForge
        PRODUCT_NAME "HEM Instrument Forge"
        COMPANY_NAME "Hybrid Elite Mafia"
        COMPANY_COPYRIGHT "Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia. All rights reserved."
        BUNDLE_ID com.hybridelitemafia.instrumentforge
        VERSION ${PROJECT_VERSION}
        ICON_BIG   ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/HEM-Icon-1024.png
        ICON_SMALL ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/HEM-Icon-1024.png)
    target_sources(InstrumentForge PRIVATE ${HF_CORE_SOURCES} ${HF_UI_SOURCES} Source/Main.cpp)
    target_include_directories(InstrumentForge PRIVATE Source)
    target_compile_definitions(InstrumentForge PRIVATE ${HF_COMMON_DEFS} HEM_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(InstrumentForge
        PRIVATE ForgeAssets ${HF_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()

if(HF_BUILD_TOOLS)
    # ModelRender: renders the modeled instruments into library folders (part of the library build)
    juce_add_console_app(ModelRender PRODUCT_NAME "ModelRender")
    target_sources(ModelRender PRIVATE Source/Library/InstrumentDef.cpp ${HF_MODEL_SOURCES} Source/Band/BandTypes.cpp Tools/ModelRender.cpp)
    target_include_directories(ModelRender PRIVATE Source)
    target_compile_definitions(ModelRender PRIVATE ${HF_COMMON_DEFS})
    target_link_libraries(ModelRender PRIVATE ${HF_JUCE_MODULES} PUBLIC juce::juce_recommended_config_flags)

    # ScreenPreview: renders every page to PNG without a display (checks the layout headless)
  if(HF_BUILD_APP)
    juce_add_console_app(ScreenPreview PRODUCT_NAME "ScreenPreview")
    target_sources(ScreenPreview PRIVATE ${HF_CORE_SOURCES} ${HF_UI_SOURCES} Tools/ScreenPreview.cpp)
    target_include_directories(ScreenPreview PRIVATE Source)
    set(HF_PREVIEW_DEFS ${HF_COMMON_DEFS})
    list(FILTER HF_PREVIEW_DEFS EXCLUDE REGEX "JUCE_MODAL_LOOPS_PERMITTED")
    target_compile_definitions(ScreenPreview PRIVATE ${HF_PREVIEW_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 HEM_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(ScreenPreview PRIVATE ForgeAssets ${HF_JUCE_MODULES} PUBLIC juce::juce_recommended_config_flags)
  endif()
endif()

if(HF_BUILD_TESTS)
    juce_add_console_app(ForgeTests PRODUCT_NAME "ForgeTests")
    target_sources(ForgeTests PRIVATE ${HF_CORE_SOURCES} ${HF_MODEL_SOURCES}
        Tests/TestMain.cpp
        Tests/TestLibrary.cpp
        Tests/LibraryTests.cpp
        Tests/SamplerTests.cpp
        Tests/StageTests.cpp
        Tests/EngineTests.cpp
        Tests/SessionTests.cpp
        Tests/BandTests.cpp
        Tests/ExportTests.cpp
        Tests/ModelTests.cpp)
    target_include_directories(ForgeTests PRIVATE Source Tests)
    target_compile_definitions(ForgeTests PRIVATE ${HF_COMMON_DEFS} JUCE_UNIT_TESTS=1)
    target_link_libraries(ForgeTests
        PRIVATE ${HF_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_link_options(ForgeTests PRIVATE /STACK:16777216)
    endif()
    enable_testing()
    add_test(NAME ForgeTests COMMAND ForgeTests)
endif()
