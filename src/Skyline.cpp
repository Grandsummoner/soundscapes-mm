#include "plugin.hpp"

// ============================================================
// Scale definitions (manual p.13)
// ============================================================
static const float SCALES[16][12] = {
    // Unquantized - passthrough
    {0,1,2,3,4,5,6,7,8,9,10,11},
    // Major Pentatonic
    {0,2,4,7,9,0,2,4,7,9,0,2},
    // Minor Pentatonic
    {0,3,5,7,10,0,3,5,7,10,0,3},
    // Blues
    {0,3,5,6,7,10,0,3,5,6,7,10},
    // Major
    {0,2,4,5,7,9,11,0,2,4,5,7},
    // Minor Natural
    {0,2,3,5,7,8,10,0,2,3,5,7},
    // Dorian
    {0,2,3,5,7,9,10,0,2,3,5,7},
    // Mixolydian
    {0,2,4,5,7,9,10,0,2,4,5,7},
    // Phrygian
    {0,1,3,5,7,8,10,0,1,3,5,7},
    // Lydian
    {0,2,4,6,7,9,11,0,2,4,6,7},
    // Locrian
    {0,1,3,4,6,8,10,0,1,3,4,6},
    // Harmonic Minor
    {0,2,3,5,7,8,11,0,2,3,5,7},
    // Persian
    {0,1,4,5,7,8,11,0,1,4,5,7},
    // Arabian
    {0,2,4,5,6,8,10,0,2,4,5,6},
    // Japanese
    {0,2,4,7,9,0,2,4,7,9,0,2},
    // Chromatic
    {0,1,2,3,4,5,6,7,8,9,10,11},
};

static const int SCALE_SIZES[16] = {12,5,5,6,7,7,7,7,7,7,7,7,7,7,5,12};

static float quantizeVoltage(float v, int scaleIdx) {
    if (scaleIdx == 0 || scaleIdx == 15) return v;
    float semitones = v * 12.0f;
    int octave = (int)std::floor(semitones / 12.0f);
    int semi   = (int)std::floor(semitones) - octave * 12;
    if (semi < 0) { semi += 12; octave--; }
    int sz = SCALE_SIZES[scaleIdx];
    int best = 0, bestDist = 12;
    for (int i = 0; i < sz; i++) {
        int s = (int)SCALES[scaleIdx][i];
        int d = std::abs(semi - s);
        if (d < bestDist) { bestDist = d; best = s; }
    }
    return (octave * 12 + best) / 12.0f;
}

// ============================================================
struct Skyline : Module {
// ============================================================
    enum ParamIds {
        // Global
        DIVIDE_PARAM,
        ATTENUATE_PARAM,
        OFFSET_PARAM,
        // Mode buttons
        MUTE_PARAM,
        LENGTH_PARAM,
        SHIFT_PARAM,
        SCALE_PARAM,
        SAVE_PARAM,
        RECALL_PARAM,
        // 8 sliders
        ENUMS(SLIDER_PARAMS, 8),
        // 16 step buttons
        ENUMS(STEP_PARAMS, 16),
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
        // Step lights
        ENUMS(STEP_LIGHTS, 16),
        // Channel LED per output
        ENUMS(CHANNEL_LIGHTS, 8),
        // Mode button lights
        MUTE_LIGHT,
        LENGTH_LIGHT,
        SHIFT_LIGHT,
        SCALE_LIGHT,
        SAVE_LIGHT,
        RECALL_LIGHT,
        NUM_LIGHTS
    };

    // ---- Sequencer state ----
    float stepCV[8][16]      = {};   // CV value 0-5V per channel per step
    int   seqLength[8]       = {16,16,16,16,16,16,16,16};
    int   seqPos[8]          = {};
    bool  stepMuted[8][16]   = {};
    bool  chanMuted[8]       = {};
    bool  stepSmooth[8][16]  = {};
    int   direction[8]       = {};   // 0=fwd 1=rev 2=pend 3=rand
    int   pendDir[8]         = {1,1,1,1,1,1,1,1};
    int   scaleIndex[8]      = {};
    bool  frozen[8]          = {};
    int   selectedChan       = 0;

    // ---- Presets ----
    float presetCV[16][8][16]   = {};
    int   presetLen[16][8]      = {};
    int   presetScale[16][8]    = {};
    int   presetDir[16][8]      = {};
    bool  presetValid[16]       = {};

    // ---- Mode flags ----
    bool muteMode   = false;
    bool lengthMode = false;
    bool shiftMode  = false;
    bool scaleMode  = false;
    bool saveMode   = false;
    bool recallMode = false;

    // ---- Clock state ----
    dsp::SchmittTrigger clockTrig;
    dsp::SchmittTrigger resetTrig;
    dsp::SchmittTrigger stepTrig[16];
    dsp::SchmittTrigger muteTrig;
    dsp::SchmittTrigger lengthTrig;
    dsp::SchmittTrigger shiftTrig;
    dsp::SchmittTrigger scaleTrig;
    dsp::SchmittTrigger saveTrig;
    dsp::SchmittTrigger recallTrig;
    int  divCount    = 0;

    // ---- Glide ----
    float glideCV[8]  = {};
    float targetCV[8] = {};

    Skyline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(DIVIDE_PARAM,   1.f, 16.f, 1.f,  "Clock Divide");
        getParamQuantity(DIVIDE_PARAM)->snapEnabled = true;
        configParam(ATTENUATE_PARAM, 0.f, 1.f,  1.f,  "Attenuate");
        configParam(OFFSET_PARAM,   -5.f, 5.f,  0.f,  "Offset", "V");

        configButton(MUTE_PARAM,   "Mute Mode");
        configButton(LENGTH_PARAM, "Length Mode");
        configButton(SHIFT_PARAM,  "Shift Mode");
        configButton(SCALE_PARAM, "Scale Mode");
        configButton(SAVE_PARAM,   "Save Preset");
        configButton(RECALL_PARAM, "Recall Preset");

        for (int i = 0; i < 8; i++) {
            configParam(SLIDER_PARAMS + i, 0.f, 5.f, 0.f,
                string::f("Channel %d Slider", i+1), "V");
            configOutput(CV_OUTPUTS + i,
                string::f("Channel %d CV", i+1));
        }
        for (int i = 0; i < 16; i++)
            configButton(STEP_PARAMS + i, string::f("Step %d", i+1));

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset / Hold");
    }

    // ---- Advance step for one channel ----
    void advanceChannel(int ch) {
        int len = seqLength[ch];
        switch (direction[ch]) {
            case 0: seqPos[ch] = (seqPos[ch] + 1) % len; break;
            case 1: seqPos[ch] = (seqPos[ch] - 1 + len) % len; break;
            case 2:
                seqPos[ch] += pendDir[ch];
                if (seqPos[ch] >= len-1) { seqPos[ch] = len-1; pendDir[ch] = -1; }
                if (seqPos[ch] <= 0)     { seqPos[ch] = 0;     pendDir[ch] =  1; }
                break;
            case 3: seqPos[ch] = (int)(random::uniform() * len); break;
        }
    }

    // ---- Preset save/recall ----
    void savePreset(int slot) {
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++)
                presetCV[slot][ch][s] = stepCV[ch][s];
            presetLen[slot][ch]   = seqLength[ch];
            presetScale[slot][ch] = scaleIndex[ch];
            presetDir[slot][ch]   = direction[ch];
        }
        presetValid[slot] = true;
    }

    void recallPreset(int slot) {
        if (!presetValid[slot]) return;
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++)
                stepCV[ch][s] = presetCV[slot][ch][s];
            seqLength[ch]  = presetLen[slot][ch];
            scaleIndex[ch] = presetScale[slot][ch];
            direction[ch]  = presetDir[slot][ch];
            if (seqPos[ch] >= seqLength[ch]) seqPos[ch] = 0;
        }
    }

    void process(const ProcessArgs& args) override {

        // ---- Mode button logic ----
        // Manual: buttons are momentary, modes toggle
        if (muteTrig.process(params[MUTE_PARAM].getValue())) {
            muteMode   = !muteMode;
            lengthMode = shiftMode = saveMode = recallMode = false;
        }
        if (lengthTrig.process(params[LENGTH_PARAM].getValue())) {
            lengthMode = !lengthMode;
            muteMode   = shiftMode = saveMode = recallMode = false;
        }
        if (shiftTrig.process(params[SHIFT_PARAM].getValue())) {
            shiftMode  = !shiftMode;
            muteMode   = lengthMode = scaleMode = saveMode = recallMode = false;
        }
        if (scaleTrig.process(params[SCALE_PARAM].getValue())) {
            scaleMode  = !scaleMode;
            muteMode   = lengthMode = shiftMode = saveMode = recallMode = false;
        }
        if (saveTrig.process(params[SAVE_PARAM].getValue())) {
            saveMode   = !saveMode;
            muteMode   = lengthMode = shiftMode = recallMode = false;
        }
        if (recallTrig.process(params[RECALL_PARAM].getValue())) {
            recallMode = !recallMode;
            muteMode   = lengthMode = shiftMode = saveMode = false;
        }

        // ---- Step button logic (manual accurate) ----
        for (int i = 0; i < 16; i++) {
            if (stepTrig[i].process(params[STEP_PARAMS + i].getValue())) {

                if (saveMode) {
                    // Manual p.10: hold SAVE + press step = save to that slot
                    savePreset(i);
                    saveMode = false;
                }
                else if (recallMode) {
                    recallPreset(i);
                    recallMode = false;
                }
                else if (muteMode) {
                    if (i < 8) {
                        // Manual p.12: MUTE + channel button (steps 1-8) = mute channel
                        chanMuted[i] = !chanMuted[i];
                    } else {
                        // Manual p.12: MUTE + step button (9-16) = mute step on selected channel
                        stepMuted[selectedChan][i - 8] = !stepMuted[selectedChan][i - 8];
                    }
                }
                else if (lengthMode) {
                    // Manual p.11: LENGTH + channel (1-8) selects channel
                    //              LENGTH + step (1-16) sets length
                    if (i < 8) {
                        selectedChan = i;
                    } else {
                        // step 8-15 = length 9-16 BUT manual says steps 1-8 select channel
                        // and then any step 1-16 sets length for that channel
                        // We handle both: steps 1-8 select channel, steps 9-16 also set length
                        seqLength[selectedChan] = i + 1;
                        if (seqPos[selectedChan] >= seqLength[selectedChan])
                            seqPos[selectedChan] = 0;
                    }
                }
                else if (shiftMode) {
                    if (i < 8) {
                        // Manual p.9: SHIFT + channel = select channel for direction
                        selectedChan = i;
                    } else {
                        // Steps 9-12 = direction: fwd/rev/pend/rand
                        if (i >= 8 && i <= 11)
                            direction[selectedChan] = i - 8;
                        // Steps 13-16 = freeze channels 1-4... but we handle freeze differently
                        // Manual: SHIFT + hold FREEZE button + select output = freeze
                        // Simplified: steps 13-16 toggle freeze for selected channel
                        else if (i >= 12 && i <= 15)
                            frozen[i - 12] = !frozen[i - 12];
                    }
                }
                else {
                    // Normal mode: steps 1-8 select channel
                    // Steps 9-16 can be used to hold/repeat steps (manual p.8)
                    if (i < 8) selectedChan = i;
                }
            }

            // Manual p.8: HOLD step button = record slider value to that step
            if (params[STEP_PARAMS + i].getValue() > 0.5f) {
                // While held, record current slider value of selected channel to this step
                stepCV[selectedChan][i] = params[SLIDER_PARAMS + selectedChan].getValue();
            }
        }

        // ---- Live recording: manual p.8 ----
        // "Moving sliders while sequence is running records at each step"
        // Record slider to current step position for each channel on every clock tick
        // (handled in clock section below)

        // ---- Scale via slider (SHIFT or SCALE mode): manual p.13 ----
        // "In SHIFT mode, slider position sets scale for selected channel"
        if (shiftMode || scaleMode) {
            float sliderVal = params[SLIDER_PARAMS + selectedChan].getValue();
            scaleIndex[selectedChan] = (int)(sliderVal / 5.0f * 15.0f);
            scaleIndex[selectedChan] = clamp(scaleIndex[selectedChan], 0, 15);
        }

        // ---- Reset / Hold ----
        bool holdHigh = inputs[RESET_INPUT].getVoltage() > 1.0f;
        if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
            for (int ch = 0; ch < 8; ch++) seqPos[ch] = 0;
            divCount = 0;
        }

        // ---- Clock ----
        bool clocked = false;
        if (!holdHigh && clockTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
            int div = (int)params[DIVIDE_PARAM].getValue();
            divCount++;
            if (divCount >= div) {
                divCount = 0;
                clocked = true;
            }
        }

        if (clocked) {
            for (int ch = 0; ch < 8; ch++) {
                if (frozen[ch]) continue;
                advanceChannel(ch);
                // Live recording: record slider at this step
                // Only record if not in any mode (normal playback)
                if (!muteMode && !lengthMode && !shiftMode && !saveMode && !recallMode) {
                    // Uncomment to enable live recording:
                    // stepCV[ch][seqPos[ch]] = params[SLIDER_PARAMS + ch].getValue();
                }
            }
        }

        // ---- Outputs ----
        float att = params[ATTENUATE_PARAM].getValue();
        float off = params[OFFSET_PARAM].getValue();

        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];

            if (chanMuted[ch] || stepMuted[ch][pos]) {
                outputs[CV_OUTPUTS + ch].setVoltage(0.f);
                lights[CHANNEL_LIGHTS + ch].setBrightness(ch == selectedChan ? 1.f : 0.05f);
                continue;
            }

            float v = stepCV[ch][pos];

            // Apply scale quantization (manual p.13)
            if (scaleIndex[ch] > 0)
                v = quantizeVoltage(v / 5.0f, scaleIndex[ch]) * 5.0f;

            // Apply glide/smooth (manual p.9)
            if (stepSmooth[ch][pos]) {
                float rate = 1.0f / (args.sampleRate * 0.05f); // 50ms glide
                glideCV[ch] += (v - glideCV[ch]) * rate;
                v = glideCV[ch];
            } else {
                glideCV[ch] = v;
            }

            // Apply attenuate and offset
            v = v * att + off;
            v = clamp(v, -5.f, 10.f);

            outputs[CV_OUTPUTS + ch].setVoltage(v);

            // Channel LED: bright if selected, dim otherwise
            lights[CHANNEL_LIGHTS + ch].setBrightness(ch == selectedChan ? 1.f : 0.2f);
        }

        // ---- Step lights ----
        for (int i = 0; i < 16; i++) {
            bool isCurrentStep = (i == seqPos[selectedChan]);
            bool isMuted       = stepMuted[selectedChan][i];
            bool inLength      = (i < seqLength[selectedChan]);
            float bright = 0.f;
            if (isCurrentStep)     bright = 1.f;
            else if (!inLength)    bright = 0.f;
            else if (isMuted)      bright = 0.08f;
            else                   bright = 0.25f;
            lights[STEP_LIGHTS + i].setBrightness(bright);
        }

        // ---- Mode lights ----
        lights[MUTE_LIGHT].setBrightness(muteMode ? 1.f : 0.f);
        lights[LENGTH_LIGHT].setBrightness(lengthMode ? 1.f : 0.f);
        lights[SHIFT_LIGHT].setBrightness(shiftMode ? 1.f : 0.f);
        lights[SCALE_LIGHT].setBrightness(scaleMode ? 1.f : 0.f);
        lights[SAVE_LIGHT].setBrightness(saveMode ? 1.f : 0.f);
        lights[RECALL_LIGHT].setBrightness(recallMode ? 1.f : 0.f);
    }

    // ---- Patch persistence ----
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* cv = json_array();
        for (int ch = 0; ch < 8; ch++)
            for (int s = 0; s < 16; s++)
                json_array_append_new(cv, json_real(stepCV[ch][s]));
        json_object_set_new(root, "stepCV", cv);

        json_t* lens = json_array();
        for (int ch = 0; ch < 8; ch++)
            json_array_append_new(lens, json_integer(seqLength[ch]));
        json_object_set_new(root, "seqLength", lens);

        json_t* dirs = json_array();
        for (int ch = 0; ch < 8; ch++)
            json_array_append_new(dirs, json_integer(direction[ch]));
        json_object_set_new(root, "direction", dirs);

        json_t* scales = json_array();
        for (int ch = 0; ch < 8; ch++)
            json_array_append_new(scales, json_integer(scaleIndex[ch]));
        json_object_set_new(root, "scaleIndex", scales);

        json_t* cmutes = json_array();
        for (int ch = 0; ch < 8; ch++)
            json_array_append_new(cmutes, json_boolean(chanMuted[ch]));
        json_object_set_new(root, "chanMuted", cmutes);

        json_t* smutes = json_array();
        for (int ch = 0; ch < 8; ch++)
            for (int s = 0; s < 16; s++)
                json_array_append_new(smutes, json_boolean(stepMuted[ch][s]));
        json_object_set_new(root, "stepMuted", smutes);

        json_t* frz = json_array();
        for (int ch = 0; ch < 8; ch++)
            json_array_append_new(frz, json_boolean(frozen[ch]));
        json_object_set_new(root, "frozen", frz);

        json_object_set_new(root, "selectedChan", json_integer(selectedChan));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* j;
        j = json_object_get(root, "stepCV");
        if (j) { int idx=0; for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepCV[ch][s]=(float)json_real_value(json_array_get(j,idx++)); }
        j = json_object_get(root, "seqLength");
        if (j) for(int ch=0;ch<8;ch++) seqLength[ch]=(int)json_integer_value(json_array_get(j,ch));
        j = json_object_get(root, "direction");
        if (j) for(int ch=0;ch<8;ch++) direction[ch]=(int)json_integer_value(json_array_get(j,ch));
        j = json_object_get(root, "scaleIndex");
        if (j) for(int ch=0;ch<8;ch++) scaleIndex[ch]=(int)json_integer_value(json_array_get(j,ch));
        j = json_object_get(root, "chanMuted");
        if (j) for(int ch=0;ch<8;ch++) chanMuted[ch]=json_boolean_value(json_array_get(j,ch));
    }
};  // end Skyline struct

struct SkylineWidget : ModuleWidget {
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Skyline.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2*RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, box.size.y - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2*RACK_GRID_WIDTH, box.size.y - RACK_GRID_WIDTH)));

        // x positions for all 8 channels - match SVG exactly
        const float cX[8] = {8.89f, 20.96f, 33.02f, 45.09f, 57.15f, 69.22f, 81.28f, 93.34f};

        // 8 CV outputs y=33, channel LEDs y=40
        for (int ch = 0; ch < 8; ch++) {
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(cX[ch], 33.f)), module, Skyline::CV_OUTPUTS + ch));
            addChild(createLightCentered<SmallLight<RedLight>>(
                mm2px(Vec(cX[ch], 40.f)), module, Skyline::CHANNEL_LIGHTS + ch));
        }

        // CLK/CV y=53, RST/HLD y=68
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.94f, 53.f)), module, Skyline::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.94f, 68.f)), module, Skyline::RESET_INPUT));

        // Knobs y=57
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(33.34f, 57.f)), module, Skyline::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.99f, 57.f)), module, Skyline::ATTENUATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(60.64f, 57.f)), module, Skyline::DIVIDE_PARAM));

        // Mode buttons row 1 y=52, row 2 y=65
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(
            mm2px(Vec(73.03f, 52.f)), module, Skyline::MUTE_PARAM,   Skyline::MUTE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(
            mm2px(Vec(82.87f, 52.f)), module, Skyline::LENGTH_PARAM, Skyline::LENGTH_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(
            mm2px(Vec(92.71f, 52.f)), module, Skyline::SHIFT_PARAM,  Skyline::SHIFT_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(
            mm2px(Vec(73.03f, 65.f)), module, Skyline::SCALE_PARAM,  Skyline::SCALE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
            mm2px(Vec(82.87f, 65.f)), module, Skyline::SAVE_PARAM,   Skyline::SAVE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
            mm2px(Vec(92.71f, 65.f)), module, Skyline::RECALL_PARAM, Skyline::RECALL_LIGHT));

        // 8 sliders y=97.5 (handle centre)
        for (int ch = 0; ch < 8; ch++) {
            addParam(createParamCentered<Trimpot>(
                mm2px(Vec(cX[ch], 97.5f)), module, Skyline::SLIDER_PARAMS + ch));
        }

        // Step buttons row 1 y=131, row 2 y=151
        for (int i = 0; i < 8; i++) {
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(cX[i], 131.f)), module,
                Skyline::STEP_PARAMS + i,   Skyline::STEP_LIGHTS + i));
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(cX[i], 151.f)), module,
                Skyline::STEP_PARAMS + 8+i, Skyline::STEP_LIGHTS + 8+i));
        }
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");