cmake_minimum_required(VERSION 3.16)

project(AudioPluginHost VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find JUCE installation - look for modules directory
# Try JUCE_DIR environment variable first
if(DEFINED ENV{JUCE_DIR})
    set(JUCE_DIR "$ENV{JUCE_DIR}")
else()
    # Default to ~/JUCE
    set(JUCE_DIR "$ENV{HOME}/JUCE")
endif()

# Construct the full path to modules
set(JUCE_MODULES_PATH "${JUCE_DIR}/modules")
set(JUCE_VST3_SDK_PATH "${JUCE_MODULES_PATH}/juce_audio_processors_headless/format_types/VST3_SDK")
set(JUCE_LV2_SDK_PATH "${JUCE_MODULES_PATH}/juce_audio_processors_headless/format_types/LV2_SDK")
set(JUCE_LV2_HEADERS_PATH "${JUCE_LV2_SDK_PATH}/lv2")

# Check if JUCE modules exist
if(NOT EXISTS "${JUCE_MODULES_PATH}/juce_core/juce_core.h")
    message(FATAL_ERROR 
        "JUCE modules not found at: ${JUCE_MODULES_PATH}\n"
        "Please set JUCE_DIR environment variable:\n"
        "  export JUCE_DIR=/path/to/JUCE\n"
        "Or install JUCE to: $HOME/JUCE")
endif()

if(NOT EXISTS "${JUCE_VST3_SDK_PATH}/pluginterfaces/base/fplatform.h")
    message(FATAL_ERROR
        "VST3 SDK headers not found at: ${JUCE_VST3_SDK_PATH}\n"
        "Expected: ${JUCE_VST3_SDK_PATH}/pluginterfaces/base/fplatform.h\n"
        "Either provide JUCE with bundled VST3 SDK or disable JUCE_PLUGINHOST_VST3 in JuceLibraryCode/AppConfig.h")
endif()

if(NOT EXISTS "${JUCE_LV2_SDK_PATH}/juce_lv2_config.h")
    message(FATAL_ERROR
        "LV2 SDK headers not found at: ${JUCE_LV2_SDK_PATH}\n"
        "Expected: ${JUCE_LV2_SDK_PATH}/juce_lv2_config.h\n"
        "Either provide JUCE LV2 SDK headers or disable JUCE_PLUGINHOST_LV2 in JuceLibraryCode/AppConfig.h")
endif()

if(NOT EXISTS "${JUCE_LV2_HEADERS_PATH}/lv2/core/lv2.h")
    message(FATAL_ERROR
        "LV2 core headers not found at: ${JUCE_LV2_HEADERS_PATH}\n"
        "Expected: ${JUCE_LV2_HEADERS_PATH}/lv2/core/lv2.h")
endif()

message(STATUS "Found JUCE at: ${JUCE_DIR}")
message(STATUS "Using JUCE modules: ${JUCE_MODULES_PATH}")

# Create the AudioPluginHost application
add_executable(AudioPluginHost)

set(JUCE_USE_FREETYPE_VALUE 0)
if(UNIX AND NOT APPLE)
    find_package(Freetype QUIET)
    if(FREETYPE_FOUND)
        set(JUCE_USE_FREETYPE_VALUE 1)
        message(STATUS "Found Freetype: ${FREETYPE_INCLUDE_DIRS}")
    else()
        message(WARNING "Freetype headers not found. Building with JUCE_USE_FREETYPE=0")
    endif()
endif()

# Set target properties
set_target_properties(AudioPluginHost PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    WIN32_EXECUTABLE TRUE)

# Include directories FIRST (before adding sources)
target_include_directories(AudioPluginHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets
    ${CMAKE_CURRENT_SOURCE_DIR}/JuceLibraryCode
    ${JUCE_MODULES_PATH}
    ${JUCE_VST3_SDK_PATH}
    ${JUCE_LV2_SDK_PATH}
    ${JUCE_LV2_HEADERS_PATH})

if(FREETYPE_FOUND)
    target_include_directories(AudioPluginHost PRIVATE ${FREETYPE_INCLUDE_DIRS})
endif()

# Add source files
target_sources(AudioPluginHost PRIVATE
    # Main application
    Source/HostStartup.cpp
    
    # Plugin handling
    Source/Plugins/ARAPlugin.cpp
    Source/Plugins/ARAPlugin.h
    Source/Plugins/IOConfigurationWindow.cpp
    Source/Plugins/IOConfigurationWindow.h
    Source/Plugins/InternalPlugins.cpp
    Source/Plugins/InternalPlugins.h
    Source/Plugins/PluginGraph.cpp
    Source/Plugins/PluginGraph.h
    
    # UI components
    Source/UI/GraphEditorPanel.cpp
    Source/UI/GraphEditorPanel.h
    Source/UI/MainHostWindow.cpp
    Source/UI/MainHostWindow.h
    Source/UI/PluginWindow.h
    
    # JUCE library code - MUST be compiled with proper headers
    JuceLibraryCode/BinaryData.cpp
    JuceLibraryCode/include_juce_core.cpp
    JuceLibraryCode/include_juce_audio_basics.cpp
    JuceLibraryCode/include_juce_audio_devices.cpp
    JuceLibraryCode/include_juce_audio_formats.cpp
    JuceLibraryCode/include_juce_audio_processors.cpp
    JuceLibraryCode/include_juce_audio_utils.cpp
    JuceLibraryCode/include_juce_cryptography.cpp
    JuceLibraryCode/include_juce_data_structures.cpp
    JuceLibraryCode/include_juce_dsp.cpp
    JuceLibraryCode/include_juce_events.cpp
    JuceLibraryCode/include_juce_graphics.cpp
    JuceLibraryCode/include_juce_gui_basics.cpp
    JuceLibraryCode/include_juce_gui_extra.cpp
    JuceLibraryCode/include_juce_opengl.cpp)

# Force include AppConfig.h for all files to provide JUCE macros
target_compile_options(AudioPluginHost PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/JuceLibraryCode/AppConfig.h)

# Compile definitions
target_compile_definitions(AudioPluginHost PRIVATE
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
    JUCE_USE_FREETYPE=${JUCE_USE_FREETYPE_VALUE})

# Compiler-specific settings
if(MSVC)
    target_compile_options(AudioPluginHost PRIVATE /W4 /WX- /FI "${CMAKE_CURRENT_SOURCE_DIR}/JuceLibraryCode/AppConfig.h")
    set_target_properties(AudioPluginHost PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
elseif(APPLE)
    target_compile_options(AudioPluginHost PRIVATE -Wall -Wextra -fPIC)
    set_target_properties(AudioPluginHost PROPERTIES 
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.juce.pluginhost")
else()
    target_compile_options(AudioPluginHost PRIVATE -Wall -Wextra -fPIC)
endif()

# Link libraries
if(WIN32)
    target_link_libraries(AudioPluginHost PRIVATE
        winmm
        ws2_32
        userenv)
elseif(APPLE)
    target_link_libraries(AudioPluginHost PRIVATE
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework CoreMidi"
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework AppKit")
else()
    target_link_libraries(AudioPluginHost PRIVATE
        pthread
        asound
        dl
        X11)

    if(FREETYPE_FOUND)
        target_link_libraries(AudioPluginHost PRIVATE ${FREETYPE_LIBRARIES})
    endif()
endif()
