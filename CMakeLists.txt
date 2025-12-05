cmake_minimum_required(VERSION 3.16)
project(AudioPluginHost VERSION 1.0.0)

# JUCE einbinden (JUCE liegt neben deinem Projekt)
add_subdirectory("${CMAKE_SOURCE_DIR}/../JUCE" "${CMAKE_BINARY_DIR}/JUCE-build")

# GUI-App definieren
juce_add_gui_app(AudioPluginHost
    PRODUCT_NAME "AudioPluginHost"
)

juce_generate_juce_header(AudioPluginHost)

# Quellen deiner App
target_sources(AudioPluginHost PRIVATE
    Source/HostStartup.cpp
    Source/Plugins/ARAPlugin.cpp
    Source/Plugins/IOConfigurationWindow.cpp
    Source/Plugins/InternalPlugins.cpp
    Source/Plugins/PluginGraph.cpp
    Source/UI/GraphEditorPanel.cpp
    Source/UI/MainHostWindow.cpp
)

# Dein eigenes BinaryData (wenn du keine Assets hast, weglassen)
juce_add_binary_data(AudioPluginHostData
    SOURCES
        Assets/icon.png
)

# Libraries & JUCE-Module
target_link_libraries(AudioPluginHost PRIVATE
    AudioPluginHostData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_gui_extra
    juce::juce_cryptography
    juce::juce_dsp
)

# Compile-Flags
target_compile_definitions(AudioPluginHost PRIVATE
    JUCE_ALSA=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_VST3=1
)
