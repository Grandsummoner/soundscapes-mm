# Path to the VCV Rack SDK directory (defaults to sibling path if not specified)
RACK_DIR ?= ../..

# Custom compiler flags
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Automatically compile all C++ source files found in our src/ directory
SOURCES += $(wildcard src/*.cpp)

# Import the core VCV Rack plugin compilation rules
include $(RACK_DIR)/plugin.mk

# CRITICAL VCV SDK RULE: Custom assets must be appended AFTER including plugin.mk
# otherwise they will be overwritten and excluded from the compiled .vcvplugin!
DISTFILES += res
