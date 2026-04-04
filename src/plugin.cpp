#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;

    // Register one line per module — must match plugin.json slugs
    // Example: p->addModel(modelMyModule);
    p->addModel(modelSkyline);
}
