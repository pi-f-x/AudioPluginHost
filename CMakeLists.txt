cmake_minimum_required(VERSION 3.16)

project(AudioPluginHost
    VERSION 1.0.0
    LANGUAGES C CXX
)

# JUCE einbinden
add_subdirectory(${CMAKE_SOURCE_DIR}/../JUCE JUCE-build)

# GUI App
juce_add_gui_app(AudioPluginHost
    PRODUCT_NAME "AudioPluginHost"
)

juce_generate_juce_header(AudioPluginHost)

# =========================
# BinaryData (aus .jucer!)
# =========================
juce_add_binary_data(AudioPluginHostData
    SOURCES
        Source/BinaryData/*
)

# Sources
target_sources(AudioPluginHost PRIVATE
    Source/HostStartup.cpp
    Source/Plugins/ARAPlugin.cpp
    Source/Plugins/IOConfigurationWindow.cpp
    Source/Plugins/InternalPlugins.cpp
    Source/Plugins/PluginGraph.cpp
    Source/UI/GraphEditorPanel.cpp
    Source/UI/MainHostWindow.cpp
)

# JUCE Module
target_link_libraries(AudioPluginHost PRIVATE
    AudioPluginHostData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_data_structures
    juce::juce_cryptography
    juce::juce_core
    juce::juce_dsp
    juce::juce_opengl
)

# Compile Defines (aus JUCEOPTIONS)
target_compile_definitions(AudioPluginHost PRIVATE
    JUCE_ALSA=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
)
