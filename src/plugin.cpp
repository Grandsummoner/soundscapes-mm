#include "plugin.hpp"

Plugin* pluginInstance;

// Global VCV Rack initialisation function.
// Wrapped in extern "C" to prevent C++ name mangling and export the pure "init" symbol.
extern "C" void init(Plugin* p) {
    pluginInstance = p;

    // Register the Soundscapes model with the central engine
    p->addModel(modelSoundscapes);
}
