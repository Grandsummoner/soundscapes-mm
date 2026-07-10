# Path to the VCV Rack SDK directory (defaults to sibling path if not specified)
RACK_DIR ?= ../..

# Custom compiler flags
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Automatically compile all C++ source files found in our src/ directory
SOURCES += $(wildcard src/*.cpp)

# Define static assets and manifests to include in the final distributable zip package
DISTFILES += res
DISTFILES += plugin.json

# Import the core VCV Rack plugin compilation rules
include $(RACK_DIR)/plugin.mk
