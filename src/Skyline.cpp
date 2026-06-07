#include "plugin.hpp"

// ============================================================
// Scale definitions (Voltage Block manual p.11)
// Index matches manual: 0=Unquant, 1=Japanese, 2=MajPenta,
// 3=MinPenta, 4=Blues, 5=Locrian, 6=Arabian, 7=Phrygian,
// 8=Minor, 9=Dorian, 10=Mixolydian, 11=Persian,
// 12=DblHarmonic, 13=Major, 14=Lydian, 15=Chromatic
// ============================================================
static const float SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},   // 0 Unquantized (passthrough)
    {0,1,5,7,10,0,1,5,7,10,0,1},   // 1 Japanese (In)
    {0,2,4,7,9,0,2,4,7,9,0,2},     // 2 Major Pentatonic
    {0,3,5,7,10,0,3,5,7,10,0,3},   // 3 Minor Pentatonic
    {0,3,5,6,7,10,0,3,5,6,7,10},   // 4 Blues
    {0,1,3,4,6,8,10,0,1,3,4,6},    // 5 Locrian
    {0,2,4,5,6,8,10,0,2,4,5,6},    // 6 Arabian
    {0,1,3,5,7,8,10,0,1,3,5,7},    // 7 Phrygian
    {0,2,3,5,7,8,10,0,2,3,5,7},    // 8 Minor Natural
    {0,2,3,5,7,9,10,0,2,3,5,7},    // 9 Dorian
    {0,2,4,5,7,9,10,0,2,4,5,7},    // 10 Mixolydian
    {0,1,4,5,7,8,11,0,1,4,5,7},    // 11 Persian
    {0,1,4,5,7,8,11,0,1,4,5,7},    // 12 Double Harmonic
    {0,2,4,5,7,9,11,0,2,4,5,7},    // 13 Major
    {0,2,4,6,7,9,11,0,2,4,6,7},    // 14 Lydian
    {0,1,2,3,4,5,6,7,8,9,10,11},   // 15 Chromatic
};
static const int SCALE_SIZES[16] = {12,5,5,5,6,7,7,7,7,7,7,7,7,7,7,12};

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
        DIVIDE_PARAM,
        ATTENUATE_PARAM,
        OFFSET_PARAM,
        CLK_SWITCH_PARAM,   // 0=CLK 1=CV 2=SLAVE
        // 6 mode buttons (all latching with lights)
        MUTE_PARAM,
        LENGTH_PARAM,
        SHIFT_PARAM,
        SCALE_PARAM,
        SAVE_PARAM,
        RECALL_PARAM,
        // 8 sliders, 16 step buttons
        ENUMS(SLIDER_PARAMS, 8),
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
        ENUMS(STEP_LIGHTS, 16),
        ENUMS(CHANNEL_LIGHTS, 8),
        MUTE_LIGHT,
        LENGTH_LIGHT,
        SHIFT_LIGHT,
        SCALE_LIGHT,
        SAVE_LIGHT,
        RECALL_LIGHT,
        NUM_LIGHTS
    };

    // ---- Sequencer state ----
    float stepCV[8][16]     = {};
    int   seqLength[8]      = {16,16,16,16,16,16,16,16};
    int   seqPos[8]         = {};
    bool  stepMuted[8][16]  = {};
    bool  chanMuted[8]      = {};
    bool  stepSmooth[8][16] = {};
    int   direction[8]      = {};   // 0=fwd 1=rev 2=pend 3=rand
    int   pendDir[8]        = {1,1,1,1,1,1,1,1};
    int   scaleIndex[8]     = {};
    bool  frozen[8]         = {};
    int   selectedChan      = 0;

    // ---- Presets (16 slots) ----
    float presetCV[16][8][16]  = {};
    int   presetLen[16][8]     = {};
    int   presetScale[16][8]   = {};
    int   presetDir[16][8]     = {};
    bool  presetValid[16]      = {};

    // ---- Mode flags (derived from latch params each tick) ----
    bool muteMode   = false;
    bool lengthMode = false;
    bool shiftMode  = false;
    bool scaleMode  = false;
    bool saveMode   = false;
    bool recallMode = false;
    // Previous-frame mode states — used to detect ON→OFF transitions
    bool prevMuteMode   = false;
    bool prevLengthMode = false;
    bool prevShiftMode  = false;
    bool prevScaleMode  = false;
    bool prevSaveMode   = false;
    bool prevRecallMode = false;

    // ---- Triggers ----
    dsp::SchmittTrigger clockTrig;
    dsp::SchmittTrigger resetTrig;
    dsp::SchmittTrigger stepTrig[16];
    // Mode buttons are latches — read value directly, no edge detect needed
    // except SAVE/RECALL which we treat as latches too via their param value
    int  divCount = 0;

    // ---- Glide ----
    float glideCV[8] = {};

    Skyline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(DIVIDE_PARAM,    1.f, 16.f, 1.f, "Clock Divide");
        getParamQuantity(DIVIDE_PARAM)->snapEnabled = true;
        configParam(ATTENUATE_PARAM, 0.f,  1.f, 1.f, "Attenuate");
        configParam(OFFSET_PARAM,   -5.f,  5.f, 0.f, "Offset", " V");
        configSwitch(CLK_SWITCH_PARAM, 0.f, 2.f, 0.f, "Clock Mode", {"CLK","CV","SLAVE"});

        // All 6 mode buttons are latches (toggle on/off, show their own LED)
        configButton(MUTE_PARAM,   "Mute");
        configButton(LENGTH_PARAM, "Length");
        configButton(SHIFT_PARAM,  "Shift");
        configButton(SCALE_PARAM,  "Scale");
        configButton(SAVE_PARAM,   "Save");
        configButton(RECALL_PARAM, "Recall");

        for (int i = 0; i < 8; i++) {
            configParam(SLIDER_PARAMS + i, 0.f, 5.f, 2.5f,
                        string::f("Ch %d CV", i+1), " V");
            configOutput(CV_OUTPUTS + i, string::f("Ch %d CV", i+1));
        }
        for (int i = 0; i < 16; i++)
            configButton(STEP_PARAMS + i, string::f("Step %d", i+1));

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset / Hold");

        // Steps default to 0V — user sets values by holding a step button
        // and moving the corresponding channel slider
    }

    // ---- Advance one channel ----
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
            for (int s = 0; s < 16; s++) presetCV[slot][ch][s] = stepCV[ch][s];
            presetLen[slot][ch]   = seqLength[ch];
            presetScale[slot][ch] = scaleIndex[ch];
            presetDir[slot][ch]   = direction[ch];
        }
        presetValid[slot] = true;
    }
    void recallPreset(int slot) {
        if (!presetValid[slot]) return;
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++) stepCV[ch][s] = presetCV[slot][ch][s];
            seqLength[ch]  = presetLen[slot][ch];
            scaleIndex[ch] = presetScale[slot][ch];
            direction[ch]  = presetDir[slot][ch];
            if (seqPos[ch] >= seqLength[ch]) seqPos[ch] = 0;
        }
    }

    void process(const ProcessArgs& args) override {

        // ================================================================
        // 1. READ MODE BUTTONS
        // VCVLightLatch holds 1.0 when latched, 0.0 when not.
        // ================================================================
        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        saveMode   = params[SAVE_PARAM].getValue()    > 0.5f;
        recallMode = params[RECALL_PARAM].getValue()  > 0.5f;

        // Detect any mode turning OFF this frame → flush all step triggers
        // so queued button edges don't fire into the wrong branch next frame.
        bool anyModeReleased = (prevMuteMode   && !muteMode)   ||
                               (prevLengthMode && !lengthMode) ||
                               (prevShiftMode  && !shiftMode)  ||
                               (prevScaleMode  && !scaleMode)  ||
                               (prevSaveMode   && !saveMode)   ||
                               (prevRecallMode && !recallMode);
        if (anyModeReleased) {
            for (int i = 0; i < 16; i++) stepTrig[i].reset();
        }
        prevMuteMode   = muteMode;
        prevLengthMode = lengthMode;
        prevShiftMode  = shiftMode;
        prevScaleMode  = scaleMode;
        prevSaveMode   = saveMode;
        prevRecallMode = recallMode;

        // Mode lights
        lights[MUTE_LIGHT].setBrightness(muteMode   ? 1.f : 0.f);
        lights[LENGTH_LIGHT].setBrightness(lengthMode ? 1.f : 0.f);
        lights[SHIFT_LIGHT].setBrightness(shiftMode  ? 1.f : 0.f);
        lights[SCALE_LIGHT].setBrightness(scaleMode  ? 1.f : 0.f);
        lights[SAVE_LIGHT].setBrightness(saveMode    ? 1.f : 0.f);
        lights[RECALL_LIGHT].setBrightness(recallMode ? 1.f : 0.f);

        // ================================================================
        // 2. STEP BUTTON COMBOS
        // Buttons 0-7  = channels 1-8  (top row)
        // Buttons 8-15 = steps    9-16 (bottom row)
        // ================================================================
        for (int i = 0; i < 16; i++) {
            if (!stepTrig[i].process(params[STEP_PARAMS + i].getValue()))
                continue;

            if (saveMode) {
                // SAVE + step button → save preset to slot i
                savePreset(i);
                params[SAVE_PARAM].setValue(0.f);   // release latch
            }
            else if (recallMode) {
                // RECALL + step button → recall preset from slot i
                recallPreset(i);
                params[RECALL_PARAM].setValue(0.f);
            }
            else if (muteMode) {
                if (i < 8) {
                    // MUTE + top-row → toggle channel mute
                    chanMuted[i] = !chanMuted[i];
                } else {
                    // MUTE + bottom-row (i=8-15) → toggle step mute for steps 8-15
                    // stepMuted is 0-indexed 0-15; bottom buttons directly map to steps 8-15
                    stepMuted[selectedChan][i] = !stepMuted[selectedChan][i];
                }
            }
            else if (lengthMode) {
                // Manual p.9: LENGTH + press the button of the last step you want.
                // "If you only want an 8 step sequence, select the 8th STEP button."
                // ALL 16 buttons set length (1-16). Top-row buttons ALSO select channel.
                if (i < 8) selectedChan = i;
                seqLength[selectedChan] = i + 1;   // i=0→length 1 … i=15→length 16
                if (seqPos[selectedChan] >= seqLength[selectedChan])
                    seqPos[selectedChan] = 0;
            }
            else if (shiftMode) {
                if (i < 8) {
                    // SHIFT + top-row → select channel for direction change
                    selectedChan = i;
                } else {
                    // SHIFT + bottom-row functions match panel label order:
                    // btn9=CLEAR, btn10=SMOOTH, btn11=RND, btn12=FREEZE,
                    // btn13=FWD,  btn14=REV,    btn15=PEND, btn16=RNDSEQ
                    switch (i) {
                        case 8:  // CLEAR: zero all steps for selected channel
                            for (int s = 0; s < 16; s++) stepCV[selectedChan][s] = 0.f;
                            break;
                        case 9:  // SMOOTH: toggle smooth on current playing step
                            stepSmooth[selectedChan][seqPos[selectedChan]] =
                                !stepSmooth[selectedChan][seqPos[selectedChan]];
                            break;
                        case 10: // RND: randomise all steps for selected channel
                            for (int s = 0; s < seqLength[selectedChan]; s++)
                                stepCV[selectedChan][s] = random::uniform() * 5.f;
                            break;
                        case 11: // FREEZE: toggle freeze on selected channel
                            frozen[selectedChan] = !frozen[selectedChan];
                            break;
                        case 12: // FWD
                            direction[selectedChan] = 0; break;
                        case 13: // REV
                            direction[selectedChan] = 1; break;
                        case 14: // PEND
                            direction[selectedChan] = 2; break;
                        case 15: // RNDSEQ
                            direction[selectedChan] = 3; break;
                        default: break;
                    }
                }
            }
            else if (scaleMode) {
                // SCALE + top-row → select channel; slider then sets scale
                if (i < 8) selectedChan = i;
            }
            else {
                // Normal: top-row selects channel
                if (i < 8) selectedChan = i;
            }
        }

        // ================================================================
        // 3. SLIDER → STEP CV RECORDING  (manual p.6 and p.8)
        // ================================================================
        // Method A — HOLD STEP BUTTON + move slider:
        //   While holding step button i, slider of selectedChan writes to stepCV[selectedChan][i]
        //   Works in normal mode only (no mode button active)
        // Method B — LIVE RECORDING on clock tick:
        //   Each clock tick, each channel's slider value is written to the step that just
        //   became active. Manual p.6: "as you move the slider the changes are recorded."
        //   Only active when no mode button is held (to avoid accidents).
        // ================================================================
        bool noMode = !muteMode && !lengthMode && !shiftMode &&
                      !scaleMode && !saveMode && !recallMode;

        // Method A: hold step button → write slider to that step immediately
        if (noMode) {
            for (int i = 0; i < 16; i++) {
                if (params[STEP_PARAMS + i].getValue() > 0.5f) {
                    // Step button held: record selectedChan's slider to step i
                    stepCV[selectedChan][i] =
                        params[SLIDER_PARAMS + selectedChan].getValue();
                }
            }
        }

        // ================================================================
        // 4. SCALE MODE: slider sets scale index for selected channel
        // ================================================================
        if (scaleMode) {
            float sv = params[SLIDER_PARAMS + selectedChan].getValue();
            scaleIndex[selectedChan] = clamp((int)(sv / 5.0f * 15.5f), 0, 15);
        }

        // ================================================================
        // 5. RESET / HOLD INPUT
        // CLK mode: rising edge → reset all to step 0
        // CV mode:  high gate → hold (don't advance)
        // ================================================================
        int  clkMode  = (int)params[CLK_SWITCH_PARAM].getValue();
        bool holdHigh = (clkMode == 1) && (inputs[RESET_INPUT].getVoltage() > 1.0f);

        if (clkMode == 0 || clkMode == 2) {
            // In CLK/SLAVE mode: rising edge on RST resets
            if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
                for (int ch = 0; ch < 8; ch++) seqPos[ch] = 0;
                divCount = 0;
            }
        }

        // ================================================================
        // 6. CLOCK / ADVANCE
        // ================================================================
        bool clocked = false;

        if (clkMode == 1) {
            // CV mode: clock input voltage maps position within each channel's length
            // 0V = step 1, 10V = last step. RST/HLD high = hold (don't update position)
            if (!holdHigh) {
                float cv = inputs[CLOCK_INPUT].getVoltage();
                for (int ch = 0; ch < 8; ch++) {
                    int len = seqLength[ch];
                    int pos = (int)clamp(cv / 10.f * (float)len, 0.f, (float)(len - 1));
                    seqPos[ch] = pos;
                }
            }
        }
        else {
            // CLK / SLAVE mode: trigger on rising edge
            if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                int div = (int)params[DIVIDE_PARAM].getValue();
                if (++divCount >= div) { divCount = 0; clocked = true; }
            }
        }

        if (clocked) {
            for (int ch = 0; ch < 8; ch++) {
                if (frozen[ch]) continue;
                advanceChannel(ch);
                // Method B: live recording — write each channel's slider to its
                // newly-active step on every clock tick (manual p.6).
                // Only when no mode button is active.
                if (noMode) {
                    stepCV[ch][seqPos[ch]] =
                        params[SLIDER_PARAMS + ch].getValue();
                }
            }
        }

        // ================================================================
        // 7. OUTPUTS
        // ================================================================
        float att = params[ATTENUATE_PARAM].getValue();
        float off = params[OFFSET_PARAM].getValue();

        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];

            // Channel LED brightness
            float ledBright = (ch == selectedChan) ? 1.0f : 0.15f;
            if (chanMuted[ch] || stepMuted[ch][pos]) ledBright = 0.0f;
            lights[CHANNEL_LIGHTS + ch].setBrightness(ledBright);

            if (chanMuted[ch] || stepMuted[ch][pos]) {
                outputs[CV_OUTPUTS + ch].setVoltage(0.f);
                continue;
            }

            float v = stepCV[ch][pos];

            // Scale quantise
            if (scaleIndex[ch] > 0 && scaleIndex[ch] < 15)
                v = quantizeVoltage(v / 5.0f, scaleIndex[ch]) * 5.0f;

            // Glide / smooth
            if (stepSmooth[ch][pos]) {
                float rate = 1.0f / (args.sampleRate * 0.05f);
                glideCV[ch] += (v - glideCV[ch]) * rate;
                v = glideCV[ch];
            } else {
                glideCV[ch] = v;
            }

            v = clamp(v * att + off, -5.f, 10.f);
            outputs[CV_OUTPUTS + ch].setVoltage(v);
        }

        // ================================================================
        // 8. STEP LIGHTS
        // Show current position on selected channel
        // ================================================================
        for (int i = 0; i < 16; i++) {
            bool isCurrent = (i == seqPos[selectedChan]);
            bool isMuted   = stepMuted[selectedChan][i];
            bool inLen     = (i < seqLength[selectedChan]);
            float b = 0.f;
            if      (isCurrent)  b = 1.f;
            else if (!inLen)     b = 0.f;
            else if (isMuted)    b = 0.05f;
            else                 b = 0.2f;
            lights[STEP_LIGHTS + i].setBrightness(b);
        }
    }

    // ================================================================
    // PATCH PERSISTENCE
    // ================================================================
    json_t* dataToJson() override {
        json_t* root = json_object();
        auto arrF = [&](const char* key, auto fn) {
            json_t* a = json_array();
            fn(a);
            json_object_set_new(root, key, a);
        };
        arrF("stepCV", [&](json_t* a){
            for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++)
                json_array_append_new(a, json_real(stepCV[ch][s]));
        });
        arrF("seqLength", [&](json_t* a){
            for (int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(seqLength[ch]));
        });
        arrF("direction", [&](json_t* a){
            for (int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(direction[ch]));
        });
        arrF("pendDir", [&](json_t* a){
            for (int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(pendDir[ch]));
        });
        arrF("scaleIndex", [&](json_t* a){
            for (int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(scaleIndex[ch]));
        });
        arrF("chanMuted", [&](json_t* a){
            for (int ch=0;ch<8;ch++) json_array_append_new(a, json_boolean(chanMuted[ch]));
        });
        arrF("stepMuted", [&](json_t* a){
            for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++)
                json_array_append_new(a, json_boolean(stepMuted[ch][s]));
        });
        arrF("stepSmooth", [&](json_t* a){
            for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++)
                json_array_append_new(a, json_boolean(stepSmooth[ch][s]));
        });
        arrF("frozen", [&](json_t* a){
            for (int ch=0;ch<8;ch++) json_array_append_new(a, json_boolean(frozen[ch]));
        });
        json_object_set_new(root, "selectedChan", json_integer(selectedChan));
        return root;
    }

    void dataFromJson(json_t* root) override {
        auto getI = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k);
            return a ? (int)json_integer_value(json_array_get(a, idx)) : 0;
        };
        auto getF = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k);
            return a ? (float)json_real_value(json_array_get(a, idx)) : 0.f;
        };
        auto getB = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k);
            return a ? json_boolean_value(json_array_get(a, idx)) : false;
        };
        int idx = 0;
        for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++)
            stepCV[ch][s] = getF("stepCV", idx++);
        for (int ch=0;ch<8;ch++) seqLength[ch]  = getI("seqLength",  ch);
        for (int ch=0;ch<8;ch++) direction[ch]   = getI("direction",  ch);
        for (int ch=0;ch<8;ch++) pendDir[ch]     = getI("pendDir",    ch);
        for (int ch=0;ch<8;ch++) scaleIndex[ch]  = getI("scaleIndex", ch);
        for (int ch=0;ch<8;ch++) chanMuted[ch]   = getB("chanMuted",  ch);
        for (int ch=0;ch<8;ch++) frozen[ch]       = getB("frozen",     ch);
        idx = 0;
        for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++)
            stepMuted[ch][s] = getB("stepMuted", idx++);
        idx = 0;
        for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++)
            stepSmooth[ch][s] = getB("stepSmooth", idx++);
        json_t* sc = json_object_get(root, "selectedChan");
        if (sc) selectedChan = (int)json_integer_value(sc);
    }

    void onRandomize(const RandomizeEvent& e) override {
        for (int ch=0;ch<8;ch++)
            for (int s=0;s<16;s++)
                stepCV[ch][s] = random::uniform() * 5.f;
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for (int ch=0;ch<8;ch++) {
            for (int s=0;s<16;s++) {
                stepCV[ch][s]     = 0.f;
                stepMuted[ch][s]  = false;
                stepSmooth[ch][s] = false;
            }
            seqLength[ch]  = 16;
            seqPos[ch]     = 0;
            direction[ch]  = 0;
            pendDir[ch]    = 1;
            scaleIndex[ch] = 0;
            chanMuted[ch]  = false;
            frozen[ch]     = false;
            glideCV[ch]    = 0.f;
        }
        selectedChan = 0;
        divCount     = 0;
    }
};


// ============================================================
// SlimFader — custom vertical fader widget
// box origin = top-left of handle travel area
// box.size.x = handleW, box.size.y = trackH + handleH
// ============================================================
struct SlimFader : app::ParamWidget {
    static const int TW = 6;    // track width px
    static const int TH = 60;   // track height px
    static const int HW = 14;   // handle width px
    static const int HH = 8;    // handle height px

    bool  dragging = false;
    float dragStartY  = 0.f;
    float dragStartVal = 0.f;

    SlimFader() { box.size = Vec(HW, TH + HH); }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float val = getParamQuantity() ? getParamQuantity()->getScaledValue() : 0.f;
        float handleY = (1.f - val) * TH;
        float tx = (box.size.x - TW) * 0.5f;

        // Track background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, tx, HH*0.5f, TW, TH, 3.f);
        nvgFillColor(args.vg, nvgRGB(0xb8,0xb5,0xae));
        nvgFill(args.vg);

        // Track fill (active portion below handle)
        nvgBeginPath(args.vg);
        nvgRect(args.vg, tx, HH*0.5f + handleY, TW, TH - handleY);
        nvgFillColor(args.vg, nvgRGB(0x99,0x20,0x20));
        nvgFill(args.vg);

        // Handle body
        float hx = (box.size.x - HW) * 0.5f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, hx, handleY, HW, HH, 2.f);
        nvgFillColor(args.vg, nvgRGB(0x30,0x30,0x30));
        nvgFill(args.vg);
        // Centre grip line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, hx+2.f, handleY+HH*0.5f);
        nvgLineTo(args.vg, hx+HW-2.f, handleY+HH*0.5f);
        nvgStrokeColor(args.vg, nvgRGB(0x80,0x80,0x80));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
    }

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging      = true;
            dragStartY    = e.pos.y;
            dragStartVal  = getParamQuantity() ? getParamQuantity()->getScaledValue() : 0.f;
            e.consume(this);
        }
        if (e.action == GLFW_RELEASE) dragging = false;
        ParamWidget::onButton(e);
    }
    void onDragMove(const DragMoveEvent& e) override {
        if (!dragging || !getParamQuantity()) return;
        float delta = -e.mouseDelta.y / (float)TH;
        dragStartVal = clamp(dragStartVal + delta, 0.f, 1.f);
        getParamQuantity()->setScaledValue(dragStartVal);
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        if (getParamQuantity()) getParamQuantity()->reset();
    }
};


// ============================================================
struct SkylineWidget : ModuleWidget {
// ============================================================
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Skyline.svg")));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2*RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2*RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // ── COORDINATE SYSTEM ───────────────────────────────────
        // 20HP panel = 101.6mm wide, SVG = 300px wide
        // scale = 300/101.6 = 2.9528 px/mm  →  mm2px(x) = x * 2.9528
        // RACK_GRID_HEIGHT = 380px = 128.6mm
        //
        // 8 channel X positions (mm) — evenly spaced with 7mm margins
        // left=7.0, right=94.6, step=12.51mm
        // ────────────────────────────────────────────────────────
        const float cX[8] = {7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};

        // Left control column x positions
        const float xJack   =  7.00f;  // CLK/CV and RST/HLD jacks
        const float xSwitch = 20.00f;  // 3-way switch (clear of jack)

        // Knob x positions (mm)
        const float xK1 = 32.0f;   // OFFSET
        const float xK2 = 48.5f;   // ATTEN
        const float xK3 = 65.0f;   // DIVIDE

        // Mode button x positions (mm) - 3 cols right side
        const float xB1 = 78.5f;
        const float xB2 = 87.0f;
        const float xB3 = 95.5f;

        // Row y positions (mm)
        const float yOut  = 22.5f;   // CV output jacks
        const float yLed  = 31.0f;   // channel LEDs
        const float yLbl  = 38.5f;   // label row above controls
        const float yClk  = 46.0f;   // CLK/CV jack
        const float yKnob = 53.5f;   // all 3 knobs + switch centre
        const float yRst  = 61.0f;   // RST/HLD jack
        const float yB1   = 46.0f;   // mode button row 1
        const float yBLbl = 54.5f;   // label between button rows
        const float yB2   = 61.0f;   // mode button row 2
        const float ySld  = 70.0f;   // slider top edge
        const float yS1   = 104.0f;  // step button row 1
        const float yS2   = 119.0f;  // step button row 2
        const float ySLbl = 126.5f;  // step function labels

        // ── CV OUTPUTS + CHANNEL LEDs ───────────────────────────
        for (int ch = 0; ch < 8; ch++) {
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(cX[ch], yOut)), module, Skyline::CV_OUTPUTS + ch));
            addChild(createLightCentered<SmallLight<RedLight>>(
                mm2px(Vec(cX[ch], yLed)), module, Skyline::CHANNEL_LIGHTS + ch));
        }

        // ── CLOCK / RESET INPUTS ────────────────────────────────
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(xJack, yClk)), module, Skyline::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(xJack, yRst)), module, Skyline::RESET_INPUT));

        // ── 3-WAY SWITCH (CLK / CV / SLAVE) ────────────────────
        // CKSSThree is ~7mm wide, ~20mm tall — centre it between the two jacks
        addParam(createParamCentered<CKSSThree>(
            mm2px(Vec(xSwitch, yKnob)), module, Skyline::CLK_SWITCH_PARAM));

        // ── KNOBS ───────────────────────────────────────────────
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(xK1, yKnob)), module, Skyline::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(xK2, yKnob)), module, Skyline::ATTENUATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(xK3, yKnob)), module, Skyline::DIVIDE_PARAM));

        // ── MODE BUTTONS (all 6 are VCVLightLatch) ──────────────
        // Row 1: MUTE (red), LENGTH (yellow), SHIFT (yellow)
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(
            mm2px(Vec(xB1, yB1)), module, Skyline::MUTE_PARAM,   Skyline::MUTE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(xB2, yB1)), module, Skyline::LENGTH_PARAM, Skyline::LENGTH_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(xB3, yB1)), module, Skyline::SHIFT_PARAM,  Skyline::SHIFT_LIGHT));
        // Row 2: SCALE (yellow), SAVE (green), RECALL (green)
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(xB1, yB2)), module, Skyline::SCALE_PARAM,  Skyline::SCALE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
            mm2px(Vec(xB2, yB2)), module, Skyline::SAVE_PARAM,   Skyline::SAVE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
            mm2px(Vec(xB3, yB2)), module, Skyline::RECALL_PARAM, Skyline::RECALL_LIGHT));

        // ── SLIDERS ─────────────────────────────────────────────
        // SlimFader box origin is top-left corner; centre horizontally on cX[ch]
        // handleW=14px=4.74mm, so offset = -4.74/2 = -2.37mm
        for (int ch = 0; ch < 8; ch++) {
            addParam(createParam<SlimFader>(
                mm2px(Vec(cX[ch] - 2.37f, ySld)), module, Skyline::SLIDER_PARAMS + ch));
        }

        // ── STEP BUTTONS (2 rows × 8) ───────────────────────────
        for (int i = 0; i < 8; i++) {
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(cX[i], yS1)), module,
                Skyline::STEP_PARAMS + i,   Skyline::STEP_LIGHTS + i));
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(cX[i], yS2)), module,
                Skyline::STEP_PARAMS + 8+i, Skyline::STEP_LIGHTS + 8+i));
        }

        // ── PANEL LABELS (NanoVG, rendered over SVG) ────────────
        struct PanelLabel : widget::Widget {
            std::string text;
            float       fontSize;
            NVGcolor    color;
            PanelLabel(Vec centre, std::string t, float sz, NVGcolor c)
                : text(t), fontSize(sz), color(c) {
                box.pos  = centre.minus(Vec(40, 8));
                box.size = Vec(80, 16);
            }
            void draw(const DrawArgs& args) override {
                nvgFontSize(args.vg, fontSize);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, color);
                nvgText(args.vg, box.size.x*0.5f, box.size.y*0.5f, text.c_str(), nullptr);
            }
        };

        auto lbl = [&](float xmm, float ymm, const char* t,
                       float sz=8.f, NVGcolor c=nvgRGB(0x33,0x33,0x33)) {
            addChild(new PanelLabel(mm2px(Vec(xmm, ymm)), t, sz, c));
        };

        // Title
        lbl(50.8f, 5.0f, "SKYLINE",                12.f, nvgRGB(0x11,0x11,0x11));
        lbl(50.8f, 9.5f, "8 CHANNEL CV SEQUENCER",  7.f, nvgRGB(0x77,0x77,0x77));

        // Channel numbers (above jacks)
        for (int i=0;i<8;i++)
            lbl(cX[i], 14.5f, std::to_string(i+1).c_str(), 9.f);

        // Left column
        lbl(xJack, yLbl,       "CLK/CV",  7.5f);
        lbl(xJack, yRst+5.5f,  "RST/HLD", 7.5f);

        // Knob labels (above knobs)
        lbl(xK1, yLbl, "OFFSET", 7.5f);
        lbl(xK2, yLbl, "ATTEN",  7.5f);
        lbl(xK3, yLbl, "DIVIDE", 7.5f);

        // Mode button labels: above row 1, between rows
        lbl(xB1, yLbl,  "MUTE",   7.5f);
        lbl(xB2, yLbl,  "LEN",    7.5f);
        lbl(xB3, yLbl,  "SHIFT",  7.5f);
        lbl(xB1, yBLbl, "SCALE",  7.5f);
        lbl(xB2, yBLbl, "SAVE",   7.5f);
        lbl(xB3, yBLbl, "RECALL", 7.5f);

        // Step function labels (below row 2)
        const char* fnLabels[8] = {"CLEAR","SMOOTH","RND","FREEZE","FWD","REV","PEND","RNDSEQ"};
        for (int i=0;i<8;i++)
            lbl(cX[i], ySLbl, fnLabels[i], 7.f);
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");
