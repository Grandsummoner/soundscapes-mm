#include "plugin.hpp"

// Scale definitions from manual pg.11
static const int NUM_SCALES = 16;
static const int SCALE_SIZES[] = {12,5,5,5,6,7,7,7,7,7,7,7,7,7,7,12};
static const int SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11}, // 1. Unquantized (chromatic used as base)
    {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1}, // 2. Japanese
    {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1}, // 3. Major Pentatonic
    {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1}, // 4. Minor Pentatonic
    {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1}, // 5. Blues
    {0,1,3,4,6,8,10,-1,-1,-1,-1,-1}, // 6. Locrian
    {0,2,4,5,7,8,10,-1,-1,-1,-1,-1}, // 7. Arabian (approximation)
    {0,1,3,4,7,8,10,-1,-1,-1,-1,-1}, // 8. Phrygian
    {0,2,3,5,7,8,10,-1,-1,-1,-1,-1}, // 9. Minor (Natural)
    {0,2,3,5,7,9,10,-1,-1,-1,-1,-1}, // 10. Dorian
    {0,2,4,5,7,9,10,-1,-1,-1,-1,-1}, // 11. Mixolydian
    {0,1,4,5,7,8,11,-1,-1,-1,-1,-1}, // 12. Persian
    {0,1,4,5,7,8,11,-1,-1,-1,-1,-1}, // 13. Double Harmonic
    {0,2,4,5,7,9,11,-1,-1,-1,-1,-1}, // 14. Major
    {0,2,4,6,7,9,11,-1,-1,-1,-1,-1}, // 15. Lydian
    {0,1,2,3,4,5,6,7,8,9,10,11}      // 16. Chromatic
};

static float quantize(float voltage, int scaleIdx) {
    if (scaleIdx == 0) return voltage; // Unquantized
    // Convert voltage to semitones (1V/oct, 0V = C4)
    float semitones = voltage * 12.0f;
    int octave = (int)std::floor(semitones / 12.0f);
    int semi = (int)std::floor(semitones) % 12;
    if (semi < 0) semi += 12;
    // Find nearest scale degree
    int sz = SCALE_SIZES[scaleIdx];
    int best = SCALES[scaleIdx][0];
    int bestDist = 12;
    for (int i = 0; i < sz; i++) {
        int s = SCALES[scaleIdx][i];
        if (s < 0) break;
        int dist = std::abs(semi - s);
        if (dist < bestDist) { bestDist = dist; best = s; }
    }
    return (octave * 12 + best) / 12.0f;
}

struct Skyline : Module {
    enum ParamIds {
        DIVIDE_PARAM,
        ATTENUATE_PARAM,
        MUTE_BUTTON_PARAM,
        LENGTH_BUTTON_PARAM,
        SCALE_BUTTON_PARAM,
        SHIFT_BUTTON_PARAM,
        SAVE_BUTTON_PARAM,
        RECALL_BUTTON_PARAM,
        ENUMS(STEP_BUTTON_PARAMS, 16),
        ENUMS(SLIDER_PARAMS, 8),
        NUM_PARAMS
    };
    enum InputIds {
        CLOCK_INPUT,
        RESET_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        ENUMS(CV_OUTPUTS, 8),
        NUM_OUTPUTS
    };
    enum LightIds {
        ENUMS(STEP_LIGHTS, 16),
        ENUMS(CHANNEL_LIGHTS, 8),
        MUTE_LIGHT,
        LENGTH_LIGHT,
        SCALE_LIGHT,
        SHIFT_LIGHT,
        NUM_LIGHTS
    };

    // Per channel state
    float stepValues[8][16] = {};   // CV value per channel per step (0-5V)
    int   stepLength[8] = {16,16,16,16,16,16,16,16};
    int   stepPos[8] = {};
    bool  stepMuted[8][16] = {};    // mute per step
    bool  channelMuted[8] = {};     // mute per channel
    bool  stepSmooth[8][16] = {};   // glide per step
    int   direction[8] = {};        // 0=fwd,1=rev,2=pend,3=rand
    int   pendDir[8];               // pendulum current direction
    int   scaleIdx[8] = {};         // scale per channel
    bool  frozen[8] = {};           // freeze per channel

    // Presets
    float presetValues[16][8][16] = {};
    int   presetLength[16][8] = {};
    int   presetScale[16][8] = {};
    int   presetDir[16][8] = {};
    bool  presetUsed[16] = {};

    // Mode buttons state
    bool muteMode = false;
    bool lengthMode = false;
    bool scaleMode = false;
    bool shiftMode = false;
    bool saveMode = false;
    bool recallMode = false;

    // Clock
    dsp::SchmittTrigger clockTrig;
    dsp::SchmittTrigger resetTrig;
    dsp::SchmittTrigger stepTriggers[16];
    dsp::SchmittTrigger muteTrig;
    dsp::SchmittTrigger lengthTrig;
    dsp::SchmittTrigger scaleTrig;
    dsp::SchmittTrigger shiftTrig;
    dsp::SchmittTrigger saveTrig;
    dsp::SchmittTrigger recallTrig;

    int selectedChannel = 0;
    int clockDivCounter = 0;
    float glideVoltage[8] = {};
    float targetVoltage[8] = {};
    bool glideActive[8] = {};

    Skyline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(DIVIDE_PARAM, 1.f, 16.f, 1.f, "Clock Divide");
        getParamQuantity(DIVIDE_PARAM)->snapEnabled = true;
        configParam(ATTENUATE_PARAM, 0.f, 1.f, 1.f, "Attenuate");
        configButton(MUTE_BUTTON_PARAM, "Mute");
        configButton(LENGTH_BUTTON_PARAM, "Length");
        configButton(SCALE_BUTTON_PARAM, "Scale");
        configButton(SHIFT_BUTTON_PARAM, "Shift");
        configButton(SAVE_BUTTON_PARAM, "Save");
        configButton(RECALL_BUTTON_PARAM, "Recall");
        for (int i = 0; i < 16; i++)
            configButton(STEP_BUTTON_PARAMS + i, string::f("Step %d", i+1));
        for (int i = 0; i < 8; i++) {
            configParam(SLIDER_PARAMS + i, 0.f, 5.f, 0.f, string::f("Channel %d", i+1), "V");
            pendDir[i] = 1;
            glideVoltage[i] = 0.f;
            targetVoltage[i] = 0.f;
        }
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset/Hold");
        for (int i = 0; i < 8; i++)
            configOutput(CV_OUTPUTS + i, string::f("Channel %d CV", i+1));
    }

    void advanceStep(int ch) {
        int len = stepLength[ch];
        switch (direction[ch]) {
            case 0: // Forward
                stepPos[ch] = (stepPos[ch] + 1) % len;
                break;
            case 1: // Reverse
                stepPos[ch] = (stepPos[ch] - 1 + len) % len;
                break;
            case 2: // Pendulum
                stepPos[ch] += pendDir[ch];
                if (stepPos[ch] >= len - 1) { stepPos[ch] = len - 1; pendDir[ch] = -1; }
                if (stepPos[ch] <= 0)       { stepPos[ch] = 0;       pendDir[ch] =  1; }
                break;
            case 3: // Random
                stepPos[ch] = (int)(random::uniform() * len);
                break;
        }
    }

    void savePreset(int slot) {
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++)
                presetValues[slot][ch][s] = stepValues[ch][s];
            presetLength[slot][ch] = stepLength[ch];
            presetScale[slot][ch]  = scaleIdx[ch];
            presetDir[slot][ch]    = direction[ch];
        }
        presetUsed[slot] = true;
    }

    void recallPreset(int slot) {
        if (!presetUsed[slot]) return;
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++)
                stepValues[ch][s] = presetValues[slot][ch][s];
            stepLength[ch] = presetLength[slot][ch];
            scaleIdx[ch]   = presetScale[slot][ch];
            direction[ch]  = presetDir[slot][ch];
        }
    }

    void process(const ProcessArgs& args) override {
        // --- Mode buttons ---
        if (muteTrig.process(params[MUTE_BUTTON_PARAM].getValue())) {
            muteMode   = !muteMode;
            lengthMode = scaleMode = shiftMode = saveMode = recallMode = false;
        }
        if (lengthTrig.process(params[LENGTH_BUTTON_PARAM].getValue())) {
            lengthMode = !lengthMode;
            muteMode   = scaleMode = shiftMode = saveMode = recallMode = false;
        }
        if (scaleTrig.process(params[SCALE_BUTTON_PARAM].getValue())) {
            scaleMode  = !scaleMode;
            muteMode   = lengthMode = shiftMode = saveMode = recallMode = false;
        }
        if (shiftTrig.process(params[SHIFT_BUTTON_PARAM].getValue())) {
            shiftMode  = !shiftMode;
            muteMode   = lengthMode = scaleMode = saveMode = recallMode = false;
        }
        if (saveTrig.process(params[SAVE_BUTTON_PARAM].getValue())) {
            saveMode   = !saveMode;
            muteMode   = lengthMode = scaleMode = shiftMode = recallMode = false;
        }
        if (recallTrig.process(params[RECALL_BUTTON_PARAM].getValue())) {
            recallMode = !recallMode;
            muteMode   = lengthMode = scaleMode = shiftMode = saveMode = false;
        }

        // --- Step buttons ---
        for (int i = 0; i < 16; i++) {
            if (stepTriggers[i].process(params[STEP_BUTTON_PARAMS + i].getValue())) {
                if (saveMode) {
                    savePreset(i);
                    saveMode = false;
                }
                else if (recallMode) {
                    recallPreset(i);
                    recallMode = false;
                }
                else if (muteMode && i < 8) {
                    // Mute channel
                    channelMuted[i] = !channelMuted[i];
                }
                else if (muteMode && i >= 8) {
                    // Mute step on selected channel
                    stepMuted[selectedChannel][i - 8] = !stepMuted[selectedChannel][i - 8];
                }
                else if (lengthMode) {
                    // Set length for selected channel (step 1-16 = length 1-16)
                    stepLength[selectedChannel] = i + 1;
                    if (stepPos[selectedChannel] >= stepLength[selectedChannel])
                        stepPos[selectedChannel] = 0;
                }
                else if (scaleMode) {
                    // Set scale for selected channel
                    scaleIdx[selectedChannel] = i;
                }
                else if (shiftMode) {
                    // Direction: steps 1-4 = fwd/rev/pend/rand
                    if (i < 4) direction[selectedChannel] = i;
                    // Steps 5-12 = freeze channels
                    else if (i >= 4 && i < 12) frozen[i - 4] = !frozen[i - 4];
                }
                else {
                    // Normal: select channel (steps 0-7) or select step to edit
                    if (i < 8) selectedChannel = i;
                }
            }
        }

        // --- Slider recording ---
        // In normal mode, slider adjusts all steps of that channel
        // Hold step button + move slider = set that step value
        for (int ch = 0; ch < 8; ch++) {
            float sliderVal = params[SLIDER_PARAMS + ch].getValue();
            // Check if any step button for this channel's steps is held
            bool stepHeld = false;
            for (int s = 0; s < 16; s++) {
                if (params[STEP_BUTTON_PARAMS + s].getValue() > 0.5f && ch == selectedChannel) {
                    stepValues[ch][s] = sliderVal;
                    stepHeld = true;
                }
            }
            // If no step held and slider moved, record to current step (live recording)
            if (!stepHeld && ch == selectedChannel) {
                // live: record to current step
                // only update if slider is being actively moved - approximate by just storing
                // (in a real implementation you'd track changes)
            }
        }

        // --- Clock & Reset ---
        bool resetHigh = inputs[RESET_INPUT].getVoltage() > 1.0f;

        if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
            for (int ch = 0; ch < 8; ch++) stepPos[ch] = 0;
            clockDivCounter = 0;
        }

        if (!resetHigh && clockTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
            int div = (int)params[DIVIDE_PARAM].getValue();
            clockDivCounter++;
            if (clockDivCounter >= div) {
                clockDivCounter = 0;
                for (int ch = 0; ch < 8; ch++) {
                    if (!frozen[ch]) advanceStep(ch);
                }
            }
        }

        // --- Outputs ---
        float att = params[ATTENUATE_PARAM].getValue();
        for (int ch = 0; ch < 8; ch++) {
            if (channelMuted[ch] || stepMuted[ch][stepPos[ch]]) {
                outputs[CV_OUTPUTS + ch].setVoltage(0.f);
                lights[CHANNEL_LIGHTS + ch].setBrightness(0.f);
                continue;
            }

            float v = stepValues[ch][stepPos[ch]];

            // Apply scale quantization
            if (scaleIdx[ch] > 0) v = quantize(v / 5.0f * 4.0f, scaleIdx[ch]) / 4.0f * 5.0f;

            // Apply glide/smooth
            if (stepSmooth[ch][stepPos[ch]]) {
                glideActive[ch] = true;
                targetVoltage[ch] = v;
            } else {
                glideActive[ch] = false;
                glideVoltage[ch] = v;
                targetVoltage[ch] = v;
            }

            if (glideActive[ch]) {
                float glideRate = 1.0f / (args.sampleRate * 0.1f); // 100ms glide
                glideVoltage[ch] += (targetVoltage[ch] - glideVoltage[ch]) * glideRate;
                v = glideVoltage[ch];
            }

            v *= att;
            outputs[CV_OUTPUTS + ch].setVoltage(clamp(v, 0.f, 5.f));
            lights[CHANNEL_LIGHTS + ch].setBrightness(v / 5.f);
        }

        // --- Step lights ---
        for (int i = 0; i < 16; i++) {
            bool active = (i == stepPos[selectedChannel]);
            bool muted  = stepMuted[selectedChannel][i];
            lights[STEP_LIGHTS + i].setBrightness(active ? 1.f : (muted ? 0.1f : 0.3f));
        }

        // Mode lights
        lights[MUTE_LIGHT].setBrightness(muteMode ? 1.f : 0.f);
        lights[LENGTH_LIGHT].setBrightness(lengthMode ? 1.f : 0.f);
        lights[SCALE_LIGHT].setBrightness(scaleMode ? 1.f : 0.f);
        lights[SHIFT_LIGHT].setBrightness(shiftMode ? 1.f : 0.f);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        // Save step values
        json_t* vals = json_array();
        for (int ch = 0; ch < 8; ch++)
            for (int s = 0; s < 16; s++)
                json_array_append_new(vals, json_real(stepValues[ch][s]));
        json_object_set_new(root, "stepValues", vals);
        // Step lengths
        json_t* lens = json_array();
        for (int ch = 0; ch < 8; ch++) json_array_append_new(lens, json_integer(stepLength[ch]));
        json_object_set_new(root, "stepLength", lens);
        // Scales
        json_t* scales = json_array();
        for (int ch = 0; ch < 8; ch++) json_array_append_new(scales, json_integer(scaleIdx[ch]));
        json_object_set_new(root, "scaleIdx", scales);
        // Directions
        json_t* dirs = json_array();
        for (int ch = 0; ch < 8; ch++) json_array_append_new(dirs, json_integer(direction[ch]));
        json_object_set_new(root, "direction", dirs);
        // Mutes
        json_t* chMutes = json_array();
        for (int ch = 0; ch < 8; ch++) json_array_append_new(chMutes, json_boolean(channelMuted[ch]));
        json_object_set_new(root, "channelMuted", chMutes);
        // Step mutes
        json_t* sMutes = json_array();
        for (int ch = 0; ch < 8; ch++)
            for (int s = 0; s < 16; s++)
                json_array_append_new(sMutes, json_boolean(stepMuted[ch][s]));
        json_object_set_new(root, "stepMuted", sMutes);
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* vals = json_object_get(root, "stepValues");
        if (vals) {
            int idx = 0;
            for (int ch = 0; ch < 8; ch++)
                for (int s = 0; s < 16; s++)
                    stepValues[ch][s] = (float)json_real_value(json_array_get(vals, idx++));
        }
        json_t* lens = json_object_get(root, "stepLength");
        if (lens) for (int ch = 0; ch < 8; ch++) stepLength[ch] = (int)json_integer_value(json_array_get(lens, ch));
        json_t* scales = json_object_get(root, "scaleIdx");
        if (scales) for (int ch = 0; ch < 8; ch++) scaleIdx[ch] = (int)json_integer_value(json_array_get(scales, ch));
        json_t* dirs = json_object_get(root, "direction");
        if (dirs) for (int ch = 0; ch < 8; ch++) direction[ch] = (int)json_integer_value(json_array_get(dirs, ch));
        json_t* chMutes = json_object_get(root, "channelMuted");
        if (chMutes) for (int ch = 0; ch < 8; ch++) channelMuted[ch] = json_boolean_value(json_array_get(chMutes, ch));
        json_t* sMutes = json_object_get(root, "stepMuted");
        if (sMutes) {
            int idx = 0;
            for (int ch = 0; ch < 8; ch++)
                for (int s = 0; s < 16; s++)
                    stepMuted[ch][s] = json_boolean_value(json_array_get(sMutes, idx++));
        }
    }
};

struct SkylineWidget : ModuleWidget {
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Skyline.svg")));

        // Mounting screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Global inputs
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.5,  22)), module, Skyline::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.5, 22)), module, Skyline::RESET_INPUT));

        // Global knobs
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(43.5, 22)), module, Skyline::DIVIDE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(54.5, 22)), module, Skyline::ATTENUATE_PARAM));

        // Channel sliders and outputs
        // Channels 1-4 with sliders
        float chY[8] = {37, 50, 63, 76, 89, 97, 105, 113};
        float sliderX = 10.5f;
        float outX = 51.0f;
        float ledX = 57.5f;

        for (int ch = 0; ch < 4; ch++) {
            addParam(createParamCentered<Trimpot>(mm2px(Vec(sliderX, chY[ch]+5)), module,
                Skyline::SLIDER_PARAMS + ch));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, chY[ch]+5)), module,
                Skyline::CV_OUTPUTS + ch));
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(ledX, chY[ch])), module,
                Skyline::CHANNEL_LIGHTS + ch));
        }

        // Channels 5-8 compact
        float compY[4] = {89, 97, 105, 113};
        for (int ch = 4; ch < 8; ch++) {
            addParam(createParamCentered<Trimpot>(mm2px(Vec(sliderX, compY[ch-4]+4)), module,
                Skyline::SLIDER_PARAMS + ch));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, compY[ch-4]+4)), module,
                Skyline::CV_OUTPUTS + ch));
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(ledX, compY[ch-4])), module,
                Skyline::CHANNEL_LIGHTS + ch));
        }

        // Function buttons
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(mm2px(Vec(8,  147)), module, Skyline::MUTE_BUTTON_PARAM,   Skyline::MUTE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(18, 147)), module, Skyline::LENGTH_BUTTON_PARAM, Skyline::LENGTH_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(28, 147)), module, Skyline::SCALE_BUTTON_PARAM,  Skyline::SCALE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(38, 147)), module, Skyline::SHIFT_BUTTON_PARAM,  Skyline::SHIFT_LIGHT));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(48, 147)), module, Skyline::SAVE_BUTTON_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(58, 147)), module, Skyline::RECALL_BUTTON_PARAM));

        // 16 Step buttons in 2 rows of 8
        for (int i = 0; i < 8; i++) {
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(5.5 + i * 7.5f, 158)), module,
                Skyline::STEP_BUTTON_PARAMS + i,
                Skyline::STEP_LIGHTS + i));
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(5.5 + i * 7.5f, 167)), module,
                Skyline::STEP_BUTTON_PARAMS + 8 + i,
                Skyline::STEP_LIGHTS + 8 + i));
        }
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");
