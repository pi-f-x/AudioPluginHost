cmake_minimum_required(VERSION 3.16)

project(AudioPluginHost
    VERSION 1.0.0
    LANGUAGES C CXX
)

# JUCE (JUCE is next to your project)
add_subdirectory(${CMAKE_SOURCE_DIR}/../JUCE JUCE-build)

# GUI app
juce_add_gui_app(AudioPluginHost
    PRODUCT_NAME "AudioPluginHost"
)

juce_generate_juce_header(AudioPluginHost)

# =========================
# BinaryData (THIS WAS MISSING)
# =========================
juce_add_binary_data(AudioPluginHostData
    SOURCES
        ./*
)

# App sources
target_sources(AudioPluginHost PRIVATE
    Source/HostStartup.cpp
    Source/Plugins/ARAPlugin.cpp
    Source/Plugins/IOConfigurationWindow.cpp
    Source/Plugins/InternalPlugins.cpp
    Source/Plugins/PluginGraph.cpp
    Source/UI/GraphEditorPanel.cpp
    Source/UI/MainHostWindow.cpp
)

# Link libraries
target_link_libraries(AudioPluginHost PRIVATE
    AudioPluginHostData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_gui_extra
    juce::juce_cryptography
    juce::juce_dsp
)

# Compile definitions
target_compile_definitions(AudioPluginHost PRIVATE
    JUCE_ALSA=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_VST3=1
)
