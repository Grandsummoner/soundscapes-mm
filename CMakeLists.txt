cmake_minimum_required(VERSION 3.22)

# Set the path to the MetaModule SDK directory
if(NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "../metamodule-plugin-sdk")
endif()

# Include the core SDK configuration
include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(soundscapes-mm VERSION 1.0.0 DESCRIPTION "Soundscapes Eurorack Synth Voice" LANGUAGES C CXX ASM)

# Add your library as a STATIC target (the SDK translates this to an mmplugin)
add_library(soundscapes-mm STATIC)

# Define your source files explicitly
target_sources(soundscapes-mm PRIVATE
    src/plugin.cpp
    src/soundscapes.cpp
    src/soundscapes_ui.cpp
    src/soundscapes_sequencer.cpp
    src/soundscapes_synth.cpp
    src/soundscapes_fx.cpp
)

# Set the include path to your local source directory
target_include_directories(soundscapes-mm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Call the official SDK macro to link VCV Rack headers and package the bundle
create_plugin(
    SOURCE_LIB soundscapes-mm
    PLUGIN_NAME soundscapes-mm
    PLUGIN_JSON ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json
)
