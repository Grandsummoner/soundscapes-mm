#include "plugin.hpp"

Plugin* pluginInstance;

// Global VCV Rack initialisation function, called on plugin load
void init(Plugin* p) {
    pluginInstance = p;

    // Register the Soundscapes model with the central engine
    p->addModel(modelSoundscapes);
}
