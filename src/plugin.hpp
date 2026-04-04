#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;
extern Model* modelSkyline;

extern float maybeDefaultContrast;
extern int maybeDefaultTheme;

void maybeLoadSettings();
void maybeSaveSettings();
void maybeApplyContrastToAll(float contrast);
void maybeApplyThemeToAll(int theme);
