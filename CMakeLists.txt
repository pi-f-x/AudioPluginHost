cmake_minimum_required(VERSION 3.22)

project(AudioPluginHost VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find JUCE
find_package(JUCE CONFIG REQUIRED)

# Create the AudioPluginHost application
juce_add_gui_app(AudioPluginHost
    COMPANY_NAME                    "Raw Material Software Limited"
    COMPANY_WEBSITE                 "https://juce.com"
    COMPANY_EMAIL                   "info@juce.com"
    BUNDLE_ID                       com.juce.pluginhost
    MICROPHONE_PERMISSION_ENABLED   TRUE
    PLUGINHOST_AU                   TRUE)

juce_generate_juce_header(AudioPluginHost)

# Target sources - all source files
target_sources(AudioPluginHost PRIVATE
    Source/HostStartup.cpp
    Source/Plugins/ARAPlugin.cpp
    Source/Plugins/ARAPlugin.h
    Source/Plugins/IOConfigurationWindow.cpp
    Source/Plugins/IOConfigurationWindow.h
    Source/Plugins/InternalPlugins.cpp
    Source/Plugins/InternalPlugins.h
    Source/Plugins/PluginGraph.cpp
    Source/Plugins/PluginGraph.h
    Source/UI/GraphEditorPanel.cpp
    Source/UI/GraphEditorPanel.h
    Source/UI/MainHostWindow.cpp
    Source/UI/MainHostWindow.h
    Source/UI/PluginWindow.h)

# Include directories
target_include_directories(AudioPluginHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets)

# Compile definitions
target_compile_definitions(AudioPluginHost PRIVATE
    JUCE_ALSA=1
    JUCE_CONTENT_SHARING=1
    JUCE_DIRECTSOUND=1
    JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_USE_CAMERA=0
    JUCE_USE_CDBURNER=0
    JUCE_USE_CDREADER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=1
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
    JUCE_WASAPI=1
    JUCE_WEB_BROWSER=0
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)

# Link libraries
target_link_libraries(AudioPluginHost PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_dsp
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
