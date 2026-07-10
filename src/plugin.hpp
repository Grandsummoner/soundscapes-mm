#pragma once
#include <rack.hpp>

using namespace rack;

// The global Plugin instance pointer, allocated by the rack main thread during loading
extern Plugin* pluginInstance;

// Model pointer representing our Soundscapes module
extern Model* modelSoundscapes;
