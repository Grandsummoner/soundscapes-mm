#include "plugin.hpp"

// Quantization Matrix
static const float SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11}, {0,1,5,7,10,0,1,5,7,10,0,1}, {0,2,4,7,9,0,2,4,7,9,0,2},
    {0,3,5,7,10,0,3,5,7,10,0,3}, {0,3,5,6,7,10,0,3,5,6,7,10}, {0,1,3,4,6,8,10,0,1,3,4,6},
    {0,2,4,5,6,8,10,0,2,4,5,6},  {0,1,3,5,7,8,10,0,1,3,5,7},  {0,2,3,5,7,8,10,0,2,3,5,7},
    {0,2,3,5,7,9,10,0,2,3,5,7},   {0,2,4,5,7,9,10,0,2,4,5,7},  {0,2,4,5,7,8,10,0,2,4,5,7},
    {0,2,4,6,8,10,0,2,4,6,8,10},  {0,1,4,5,7,8,11,0,1,4,5,7},  {0,1,3,5,6,9,10,0,1,3,5,6},
    {0,1,4,5,8,9,0,1,4,5,8,9}
};

struct Soundscape : Module {
    enum ParamIds {
        MASTER_CLOCK_PARAM,
        MASTER_OFFSET_PARAM,
        MASTER_DENSITY_PARAM,
        MODE_TOGGLE_PARAM,
        FADER_PARAMS,
        DISPLAY_BTN_PARAMS = FADER_PARAMS + 8,
        MACRO_RIGHT_PARAMS = DISPLAY_BTN_PARAMS + 8,
        PERF_GRID_PARAMS = MACRO_RIGHT_PARAMS + 6,
        NUM_PARAMS = PERF_GRID_PARAMS + 16
    };
    enum InputIds { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { CV_OUTPUTS, NUM_OUTPUTS = 8 };
    enum LightIds { CHANNEL_LEDS, NUM_LIGHTS = 8 };

    // Operational Architectures
    enum EngineModes { MODE_INT, MODE_EXT, MODE_MOD };
    enum ChannelModes { CH_CV, CH_PITCH, CH_GATE };

    // State Variables
    int globalMode = MODE_INT;
    int chModes[8] = {CH_CV, CH_CV, CH_PITCH, CH_PITCH, CH_CV, CH_CV, CH_GATE, CH_GATE};
    float currentVoltages[8] = {0.f};
    int stepCounters[8] = {0};
    
    // UI Handling & Timeouts
    float vuTimeoutCounter = 0.f;
    bool isTweakActive = false;
    float lastClockVal = 0.f;
    float lastResetVal = 0.f;
    float internalClockTimer = 0.f;

    // Button Edge Triggers
    dsp::SchmittTrigger modeButtons[8];
    dsp::SchmittTrigger globalModeToggle;

    Soundscape() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        // Global Controls
        configParam(MASTER_CLOCK_PARAM, 0.f, 1.f, 0.5f, "Sync / Clock Scaler");
        configParam(MASTER_OFFSET_PARAM, 0.f, 1.f, 0.5f, "Offset / Performance Bias");
        configParam(MASTER_DENSITY_PARAM, 0.f, 1.f, 0.8f, "Rhythmic Density / Step Prob");
        configParam(MODE_TOGGLE_PARAM, 0.f, 2.f, 0.f, "Global Mode (INT, EXT, MOD)");

        // Arrays
        for (int i = 0; i < 8; i++) {
            configParam(FADER_PARAMS + i, 0.f, 1.f, 0.f, "Channel Fader " + std::to_string(i + 1));
            configParam(DISPLAY_BTN_PARAMS + i, 0.f, 1.f, 0.f, "Channel Mode Button " + std::to_string(i + 1));
            configInput(CLOCK_INPUT + i, "Clock Input");
            configOutput(CV_OUTPUTS + i, "CV Output " + std::to_string(i + 1));
        }
        
        for (int i = 0; i < 16; i++) {
            configParam(PERF_GRID_PARAMS + i, 0.f, 1.f, 0.f, "Performance Grid Button " + std::to_string(i + 1));
        }
    }

    void process(const ProcessArgs& args) override {
        // 1. Process Global Master Mode Selection
        int nextMode = (int)params[MODE_TOGGLE_PARAM].getValue();
        if (nextMode != globalMode) {
            globalMode = nextMode;
        }

        // 2. Track Knob Tweaks for UI Bar Graph Display Flip
        static float pClk = -1.f, pOff = -1.f, pDen = -1.f;
        float cClk = params[MASTER_CLOCK_PARAM].getValue();
        float cOff = params[MASTER_OFFSET_PARAM].getValue();
        float cDen = params[MASTER_DENSITY_PARAM].getValue();

        if ((pClk >= 0.f && std::abs(cClk - pClk) > 0.001f) ||
            (pOff >= 0.f && std::abs(cOff - pOff) > 0.001f) ||
            (pDen >= 0.f && std::abs(cDen - pDen) > 0.001f)) {
            isTweakActive = true;
            vuTimeoutCounter = 0.5f; // Keep on for 500ms
        }
        pClk = cClk; pOff = cOff; pDen = cDen;

        if (isTweakActive) {
            vuTimeoutCounter -= args.sampleTime;
            if (vuTimeoutCounter <= 0.f) {
                isTweakActive = false;
            }
        }

        // 3. Process Channel Sub-Modes (C-P-G Selection Loop)
        for (int i = 0; i < 8; i++) {
            if (modeButtons[i].process(params[DISPLAY_BTN_PARAMS + i].getValue())) {
                chModes[i] = (chModes[i] + 1) % 3;
            }
        }

        // 4. Compute Engine Core Logic Based on Mode Selection
        bool clockTriggered = false;
        bool resetTriggered = false;

        if (inputs[RESET_INPUT].isConnected()) {
            float rVal = inputs[RESET_INPUT].getVoltage();
            if (rVal > 2.0f && lastResetVal <= 2.0f) resetTriggered = true;
            lastResetVal = rVal;
        }

        if (globalMode == MODE_INT) {
            float baseBPM = params[MASTER_CLOCK_PARAM].getValue() * 240.f + 40.f;
            internalClockTimer += args.sampleTime;
            float stepDuration = 60.f / (baseBPM * 4.f); // 16th resolution basic
            if (internalClockTimer >= stepDuration) {
                internalClockTimer = 0.f;
                clockTriggered = true;
            }
        } else if (globalMode == MODE_EXT && inputs[CLOCK_INPUT].isConnected()) {
            float clkIn = inputs[CLOCK_INPUT].getVoltage();
            if (clkIn > 2.0f && lastClockVal <= 2.0f) {
                clockTriggered = true;
            }
            lastClockVal = clkIn;
        }

        // 5. Sequence Processing and Voltage Scaling/Quantization
        float globalOffset = (params[MASTER_OFFSET_PARAM].getValue() - 0.5f) * 10.f; // +/- 5V Shift
        float densityThreshold = params[MASTER_DENSITY_PARAM].getValue();

        for (int i = 0; i < 8; i++) {
            if (resetTriggered) {
                stepCounters[i] = 0;
            } else if (clockTriggered) {
                // Check probability gate logic (Knob 3)
                float rolledProb = (float)rand() / (float)RAND_MAX;
                if (rolledProb <= densityThreshold) {
                    stepCounters[i] = (stepCounters[i] + 1) % 16;
                }
            }

            // Extract fader source configuration
            float rawVoltage = params[FADER_PARAMS + i].getValue() * 10.f; // 0 to 10V
            
            // Modulation sample & hold condition
            if (globalMode == MODE_MOD) {
                rawVoltage += globalOffset;
                if (rawVoltage > 10.f) rawVoltage = 10.f;
                if (rawVoltage < 0.f) rawVoltage = 0.f;
            } else {
                rawVoltage += globalOffset;
            }

            // Final C-P-G Conditioning Pipeline
            if (chModes[i] == CH_PITCH) {
                // Apply Scale Index Chromatic Locking
                int semi = std::round(rawVoltage * 12.f);
                int oct = semi / 12;
                int note = semi % 12;
                currentVoltages[i] = (float)oct + ((float)note / 12.f);
            } else if (chModes[i] == CH_GATE) {
                currentVoltages[i] = (rawVoltage > 2.5f) ? 10.f : 0.f;
            } else {
                currentVoltages[i] = rawVoltage; // CV Linear Mode
            }

            outputs[CV_OUTPUTS + i].setVoltage(currentVoltages[i]);
            lights[CHANNEL_LEDS + i].setBrightness(currentVoltages[i] / 10.f);
        }
    }
};

// Custom Multi-Purpose Custom UI Display Vector Layouts
struct SoundscapeDisplay : LightWidget {
    Soundscape* module;
    int channelId;
    
    SoundscapeDisplay() {}
    
    void draw(const DrawArgs& args) override {
        // Canvas Setup
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, 24, 32);
        nvgFillColor(args.vg, nvgRGBA(0x1a, 0x1a, 0x1a, 0xff));
        nvgFill(args.vg);
        
        if (!module) return;

        nvgStrokeWidth(args.vg, 2.5f);
        nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Amber LED lines

        if (module->isTweakActive) {
            // RENDER TRANSIENT ADJUSTMENT VU METER GRAPHICS
            float strength = module->params[Soundscape::FADER_PARAMS + channelId].getValue();
            int bars = std::round(strength * 4.f) + 1;
            
            for (int b = 0; b < bars; b++) {
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 4, 26 - (b * 6));
                nvgLineTo(args.vg, 20, 26 - (b * 6));
                nvgStroke(args.vg);
            }
        } else {
            // RENDER IDLE C-P-G MODAL STATUS CHARACTERS
            int currentMode = module->chModes[channelId];
            nvgBeginPath(args.vg);
            if (currentMode == Soundscape::CH_CV) {
                // Draw C character
                nvgMoveTo(args.vg, 18, 8); nvgLineTo(args.vg, 6, 8);
                nvgLineTo(args.vg, 6, 24); nvgLineTo(args.vg, 18, 24);
            } else if (currentMode == Soundscape::CH_PITCH) {
                // Draw P character
                nvgMoveTo(args.vg, 6, 24); nvgLineTo(args.vg, 6, 8);
                nvgLineTo(args.vg, 18, 8); nvgLineTo(args.vg, 18, 16);
                nvgLineTo(args.vg, 6, 16);
            } else if (currentMode == Soundscape::CH_GATE) {
                // Draw G character
                nvgMoveTo(args.vg, 18, 8); nvgLineTo(args.vg, 6, 8);
                nvgLineTo(args.vg, 6, 24); nvgLineTo(args.vg, 18, 24);
                nvgLineTo(args.vg, 18, 16); nvgLineTo(args.vg, 12, 16);
            }
            nvgStroke(args.vg);
        }
    }
};

struct SoundscapeWidget : ModuleWidget {
    SoundscapeWidget(Soundscape* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Soundscape.svg")));

        // 1. Top Panel Connectivity Inputs
        addInput(createInputCentered<SvgPort>(mm2px(Vec(12.5f, 15.f)), module, Soundscape::CLOCK_INPUT));
        addInput(createInputCentered<SvgPort>(mm2px(Vec(12.5f, 32.f)), module, Soundscape::RESET_INPUT));

        // 2. Global Functional Mode Toggle Configuration
        addParam(createParamCentered<SvgSwitch>(mm2px(Vec(42.5f, 23.5f)), module, Soundscape::MODE_TOGGLE_PARAM));

        // 3. Complete Integration of the 3 Command Knobs
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(95.f, 20.f)), module, Soundscape::MASTER_CLOCK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(142.f, 20.f)), module, Soundscape::MASTER_OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(190.f, 20.f)), module, Soundscape::MASTER_DENSITY_PARAM));

        // 4. Macro Right Button Matrix Array Placement
        float macroX = 265.f;
        float macroYTop = 15.f;
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 3; c++) {
                addParam(createParamCentered<VCVButton>(mm2px(Vec(macroX + (c * 12.f), macroYTop + (r * 15.f))), module, Soundscape::MACRO_RIGHT_PARAMS + (r * 3 + c)));
            }
        }

        // 5. Channel Strip Matrix (Top-Down Execution Grid)
        float startX = 20.7f;
        float stepX = 36.9f;

        for (int i = 0; i < 8; i++) {
            float currentX = startX + (i * stepX);

            // Row 1: The 7-Segment Combined Selection Buttons
            SoundscapeDisplay* display = createWidget<SoundscapeDisplay>(mm2px(Vec(currentX - 4.f, 60.f)));
            display->module = module;
            display->channelId = i;
            addChild(display);
            addParam(createParamCentered<VCVButton>(mm2px(Vec(currentX, 66.f)), module, Soundscape::DISPLAY_BTN_PARAMS + i));

            // Row 2: Diagnostic Indicator LED Layer
            addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(currentX, 82.f)), module, Soundscape::CHANNEL_LEDS + i));

            // Row 3: Output Structural Connection Array
            addOutput(createOutputCentered<SvgPort>(mm2px(Vec(currentX, 96.f)), module, Soundscape::CV_OUTPUTS + i));

            // Row 4: Dedicated Linear Manual Attenuation Performance Faders
            addParam(createParamCentered<SvgSlider>(mm2px(Vec(currentX, 150.f)), module, Soundscape::FADER_PARAMS + i));
        }

        // 6. Complete Reconstruction of the 16 Bottom Performance Buttons Grid
        float lowerY1 = 230.f;
        float lowerY2 = 255.f;
        for (int i = 0; i < 8; i++) {
            float currentX = startX + (i * stepX);
            addParam(createParamCentered<VCVButton>(mm2px(Vec(currentX, lowerY1)), module, Soundscape::PERF_GRID_PARAMS + i));
            addParam(createParamCentered<VCVButton>(mm2px(Vec(currentX, lowerY2)), module, Soundscape::PERF_GRID_PARAMS + 8 + i));
        }
    }
};

Model* modelSoundscape = createModel<Soundscape, SoundscapeWidget>("SoundscapeMM");
