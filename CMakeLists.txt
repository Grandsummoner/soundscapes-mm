cmake_minimum_required(VERSION 3.22)

# Default path to the metamodule-plugin-sdk if not passed as a cmake command argument
if(NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "../metamodule-plugin-sdk")
endif()

# Include the core compilation directives provided by 4ms
include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(soundscapes-mm
    VERSION 1.0.0
    DESCRIPTION "8-Channel Poly-engine for VCV Rack and 4ms MetaModule"
    LANGUAGES C CXX ASM
)

# Define static compilation library
add_library(soundscapes-mm STATIC)

# Establish header search locations
target_include_directories(soundscapes-mm PRIVATE
    .
    src
)

# Collect all core C++ source implementation files
target_sources(soundscapes-mm PRIVATE
    src/plugin.cpp
    src/soundscapes.cpp
    src/soundscapes_ui.cpp
    src/soundscapes_sequencer.cpp
    src/soundscapes_synth.cpp
    src/soundscapes_fx.cpp
)

# Invoke the SDK packaging macro. 
# Note: Added the DESTINATION argument to resolve the cmake_path error in plugin.cmake.
create_plugin(
    SOURCE_LIB soundscapes-mm
    PLUGIN_NAME soundscapes-mm
    PLUGIN_JSON ${CMAKE_CURRENT_LIST_DIR}/plugin.json
    SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/res
    DESTINATION ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
)
