# If RACK_DIR is not defined when calling make, default to the SDK directory
RACK_DIR ?= ../..

# FLAGS will be passed to both C and C++ compilers
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# List of source files to compile
SOURCES += $(wildcard src/*.cpp)

# List of helper files to include in distribution (SVG panel, fonts, and metadata)
# Note: DISTRIBUTABLES is the standard VCV SDK variable used to pack the assets!
DISTRIBUTABLES += plugin.json res/

# Include the standard Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
