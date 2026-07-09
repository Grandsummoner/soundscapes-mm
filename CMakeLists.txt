cmake_minimum_required(VERSION 3.22)

# Set the path to the MetaModule SDK directory
if(NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "../metamodule-plugin-sdk")
endif()

# Include the core SDK configuration
include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(soundscapes-mm VERSION 1.0.0 DESCRIPTION "Soundscapes Eurorack Synth Voice" LANGUAGES C CXX ASM)

# Automatically find all C/C++ source files in the src/ directory.
# This prevents case-sensitivity errors (e.g., Soundscape.cpp vs soundscapes.cpp)
# and automatically scales if you split or rename files later.
file(GLOB_RECURSE PLUGIN_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)

# Add your library as a STATIC target using the automatically discovered sources
add_library(soundscapes-mm STATIC ${PLUGIN_SOURCES})

# Set the include path to your local source directory
target_include_directories(soundscapes-mm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Call the official SDK macro to link VCV Rack headers and package the bundle
create_plugin(
    SOURCE_LIB soundscapes-mm
    PLUGIN_NAME soundscapes-mm
    PLUGIN_JSON ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json
    SOURCE_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/res
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build
)
