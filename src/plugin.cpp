#include "plugin.hpp"

// Initialize the universal shared plugin binary memory instance
Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;

    // Register corrected structural target execution hooks
    p->addModel(modelSoundscape);
}
