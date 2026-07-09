#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;

    // Registers the Soundscapes model with the VCV system
    p->addModel(modelSoundscapes);
}
