#include "plugin.hpp"

// ============================================================
// Scales (Voltage Block manual p.11)
// ============================================================
static const float SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},  // 0  Unquantized
    {0,1,5,7,10,0,1,5,7,10,0,1},  // 1  Japanese (In)
    {0,2,4,7,9,0,2,4,7,9,0,2},    // 2  Major Pentatonic
    {0,3,5,7,10,0,3,5,7,10,0,3},  // 3  Minor Pentatonic
    {0,3,5,6,7,10,0,3,5,6,7,10},  // 4  Blues
    {0,1,3,4,6,8,10,0,1,3,4,6},   // 5  Locrian
    {0,2,4,5,6,8,10,0,2,4,5,6},   // 6  Arabian
    {0,1,3,5,7,8,10,0,1,3,5,7},   // 7  Phrygian
    {0,2,3,5,7,8,10,0,2,3,5,7},   // 8  Natural Minor
    {0,2,3,5,7,9,10,0,2,3,5,7},   // 9  Dorian
    {0,2,4,5,7,9,10,0,2,4,5,7},   // 10 Mixolydian
    {0,1,4,5,7,8,11,0,1,4,5,7},   // 11 Persian
    {0,1,4,5,7,8,11,0,1,4,5,7},   // 12 Double Harmonic
    {0,2,4,5,7,9,11,0,2,4,5,7},   // 13 Major
    {0,2,4,6,7,9,11,0,2,4,6,7},   // 14 Lydian
    {0,1,2,3,4,5,6,7,8,9,10,11},  // 15 Chromatic
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
        int d = std::abs(semi - (int)SCALES[scaleIdx][i]);
        if (d < bestDist) { bestDist = d; best = (int)SCALES[scaleIdx][i]; }
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
        CLK_SWITCH_PARAM,
        MUTE_PARAM,
        LENGTH_PARAM,
        SHIFT_PARAM,
        SCALE_PARAM,
        SAVE_PARAM,
        RECALL_PARAM,
        ENUMS(SLIDER_PARAMS, 8),
        ENUMS(STEP_PARAMS, 16),
        NUM_PARAMS
    };
    enum InputIds  { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { ENUMS(CV_OUTPUTS, 8), NUM_OUTPUTS };
    enum LightIds  {
        ENUMS(STEP_LIGHTS, 16),    // red  — playhead position
        ENUMS(EDIT_LIGHTS, 16),    // yellow — editStep lock indicator
        ENUMS(CHANNEL_LIGHTS, 8),
        MUTE_LIGHT, LENGTH_LIGHT, SHIFT_LIGHT,
        SCALE_LIGHT, SAVE_LIGHT, RECALL_LIGHT,
        NUM_LIGHTS
    };

    // ── Sequencer state ──────────────────────────────────────
    float stepCV[8][16]    = {};
    int   seqLength[8]     = {16,16,16,16,16,16,16,16};
    int   seqPos[8]        = {};
    bool  stepMuted[8][16] = {};
    bool  chanMuted[8]     = {};
    bool  stepSmooth[8][16]= {};
    int   direction[8]     = {};
    int   pendDir[8]       = {1,1,1,1,1,1,1,1};
    int   scaleIndex[8]    = {};
    bool  frozen[8]        = {};
    int   selectedChan     = 0;

    // Live recording enable per channel (right-click toggle)
    bool liveRecord[8] = {};

    // Slider values captured when LENGTH mode is first latched.
    // seqLength only updates if slider moves > deadband from snapshot.
    float lengthSliderSnapshot[8] = {-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f};
    static constexpr float LENGTH_DEADBAND = 0.15f;  // ~0.5 steps

    // Slider value captured when SCALE mode is first latched.
    // scaleIndex only updates if slider moves > deadband from snapshot.
    float scaleSliderSnapshot = -1.f;
    static constexpr float SCALE_DEADBAND = 0.15f;  // ~0.5 scale steps

    // ── Presets ───────────────────────────────────────────────
    float presetCV[16][8][16] = {};
    int   presetLen[16][8]    = {};
    int   presetScale[16][8]  = {};
    int   presetDir[16][8]    = {};
    bool  presetValid[16]     = {};

    // ── Mode flags ────────────────────────────────────────────
    bool muteMode=false, lengthMode=false, shiftMode=false;
    bool scaleMode=false, saveMode=false,  recallMode=false;
    bool prevMuteMode=false, prevLengthMode=false, prevShiftMode=false;
    bool prevScaleMode=false, prevSaveMode=false,  prevRecallMode=false;

    // editStep: which step is currently locked for slider editing.
    // -1 = no step locked. Click a step button (normal mode) to lock it,
    // click it again to unlock. While locked, all 8 sliders immediately
    // write to stepCV[ch][editStep] for all 8 channels every frame.
    int editStep = -1;
    int prevSelectedChan = 0;  // track channel changes for SCALE re-snapshot

    // ── Triggers ─────────────────────────────────────────────
    dsp::SchmittTrigger clockTrig, resetTrig, stepTrig[16];
    int divCount = 0;
    float glideCV[8] = {};

    Skyline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(DIVIDE_PARAM,    1.f, 16.f, 1.f,  "Clock Divide");
        getParamQuantity(DIVIDE_PARAM)->snapEnabled = true;
        configParam(ATTENUATE_PARAM, 0.f,  1.f, 1.f,  "Attenuate");
        configParam(OFFSET_PARAM,   -5.f,  5.f, 0.f,  "Offset", " V");
        configSwitch(CLK_SWITCH_PARAM, 0.f, 2.f, 0.f, "Clock Mode", {"CLK","CV","SLAVE"});
        configButton(MUTE_PARAM,   "Mute");
        configButton(LENGTH_PARAM, "Length");
        configButton(SHIFT_PARAM,  "Shift");
        configButton(SCALE_PARAM,  "Scale");
        configButton(SAVE_PARAM,   "Save");
        configButton(RECALL_PARAM, "Recall");
        for (int i = 0; i < 8;  i++) {
            configParam(SLIDER_PARAMS + i, 0.f, 5.f, 0.f, string::f("Ch %d Slider", i+1), " V");
            configOutput(CV_OUTPUTS + i, string::f("Ch %d CV", i+1));
        }
        for (int i = 0; i < 16; i++)
            configButton(STEP_PARAMS + i, string::f("Step %d", i+1));
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset / Hold");
    }

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

        // ============================================================
        // 1. READ MODE LATCHES
        // ============================================================
        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        saveMode   = params[SAVE_PARAM].getValue()    > 0.5f;
        recallMode = params[RECALL_PARAM].getValue()  > 0.5f;

        // Flush step triggers on any mode release to prevent bleed
        // Also clear editStep when entering any mode
        bool anyActivated = (!prevMuteMode   && muteMode)   ||
                            (!prevLengthMode && lengthMode) ||
                            (!prevShiftMode  && shiftMode)  ||
                            (!prevScaleMode  && scaleMode)  ||
                            (!prevSaveMode   && saveMode)   ||
                            (!prevRecallMode && recallMode);
        if (anyActivated) editStep = -1;

        // Capture slider positions the moment LENGTH is latched —
        // length only changes if user moves slider AWAY from that position
        if (!prevLengthMode && lengthMode) {
            for (int ch = 0; ch < 8; ch++)
                lengthSliderSnapshot[ch] = params[SLIDER_PARAMS + ch].getValue();
        }
        if (prevLengthMode && !lengthMode) {
            for (int ch = 0; ch < 8; ch++)
                lengthSliderSnapshot[ch] = -1.f;
        }

        // Same deadband protection for SCALE.
        // Re-snapshot whenever: SCALE is first latched, OR selectedChan changes
        // while SCALE is already active (so switching channels doesn't jump).
        bool scaleChannelChanged = scaleMode && (selectedChan != prevSelectedChan);
        if ((!prevScaleMode && scaleMode) || scaleChannelChanged)
            scaleSliderSnapshot = params[SLIDER_PARAMS + selectedChan].getValue();
        if (prevScaleMode && !scaleMode)
            scaleSliderSnapshot = -1.f;
        prevSelectedChan = selectedChan;

        bool anyReleased = (prevMuteMode   && !muteMode)   ||
                           (prevLengthMode && !lengthMode) ||
                           (prevShiftMode  && !shiftMode)  ||
                           (prevScaleMode  && !scaleMode)  ||
                           (prevSaveMode   && !saveMode)   ||
                           (prevRecallMode && !recallMode);
        if (anyReleased)
            for (int i = 0; i < 16; i++) stepTrig[i].reset();

        prevMuteMode=muteMode; prevLengthMode=lengthMode; prevShiftMode=shiftMode;
        prevScaleMode=scaleMode; prevSaveMode=saveMode; prevRecallMode=recallMode;

        lights[MUTE_LIGHT].setBrightness(muteMode   ? 1.f : 0.f);
        lights[LENGTH_LIGHT].setBrightness(lengthMode ? 1.f : 0.f);
        lights[SHIFT_LIGHT].setBrightness(shiftMode  ? 1.f : 0.f);
        lights[SCALE_LIGHT].setBrightness(scaleMode  ? 1.f : 0.f);
        lights[SAVE_LIGHT].setBrightness(saveMode    ? 1.f : 0.f);
        lights[RECALL_LIGHT].setBrightness(recallMode ? 1.f : 0.f);

        bool noMode = !muteMode && !lengthMode && !shiftMode &&
                      !scaleMode && !saveMode && !recallMode;

        // ============================================================
        // 2. STEP BUTTON COMBOS
        //
        // Hardware workflow vs VCV mouse workflow:
        // ─────────────────────────────────────────────────────────────
        // REAL MODULE: user holds a step button with one finger while
        //   adjusting sliders with the other hand simultaneously.
        //
        // VCV MOUSE: can't hold+drag simultaneously. Solution:
        //   • Top-row click (no mode) = SELECT that channel (selectedChan)
        //     AND lock to that step for editing (editStep = i)
        //   • While editStep is set, all slider changes immediately write
        //     to stepCV[ch][editStep] for ALL 8 channels
        //   • Click the same step again OR click another step to change
        //   • editStep = -1 means no step is locked for editing
        //
        // LENGTH mode:
        //   Real module: LENGTH latch → move slider → slider position
        //   sets length. Manual overview p.5: "Select and then adjust a
        //   step slider to control the amount of steps per sequence."
        //   In VCV: LENGTH latch → each channel's slider controls its OWN
        //   length. Slider 0V→1 step, 5V→16 steps. Top-row step buttons
        //   still select which channel is shown on step lights.
        // ============================================================

        for (int i = 0; i < 16; i++) {
            if (!stepTrig[i].process(params[STEP_PARAMS + i].getValue()))
                continue;

            if (saveMode) {
                savePreset(i);
                params[SAVE_PARAM].setValue(0.f);
            }
            else if (recallMode) {
                recallPreset(i);
                params[RECALL_PARAM].setValue(0.f);
            }
            else if (muteMode) {
                if (i < 8)
                    chanMuted[i] = !chanMuted[i];
                else
                    // Bottom row maps to steps 8-15 of selectedChan
                    stepMuted[selectedChan][i] = !stepMuted[selectedChan][i];
            }
            else if (lengthMode) {
                // Top-row: select which channel to view/edit
                if (i < 8) selectedChan = i;
                // No step-button-sets-length here — sliders handle that below
            }
            else if (shiftMode) {
                if (i < 8) {
                    selectedChan = i;
                } else {
                    // Bottom row = CLEAR SMOOTH RND FREEZE FWD REV PEND RNDSEQ
                    switch (i) {
                        case 8:  for (int s=0;s<16;s++) stepCV[selectedChan][s]=0.f; break;
                        case 9:  stepSmooth[selectedChan][seqPos[selectedChan]]=
                                     !stepSmooth[selectedChan][seqPos[selectedChan]]; break;
                        case 10: for (int s=0;s<seqLength[selectedChan];s++)
                                     stepCV[selectedChan][s]=random::uniform()*5.f; break;
                        case 11: frozen[selectedChan]=!frozen[selectedChan]; break;
                        case 12: direction[selectedChan]=0; break;
                        case 13: direction[selectedChan]=1; break;
                        case 14: direction[selectedChan]=2; break;
                        case 15: direction[selectedChan]=3; break;
                        default: break;
                    }
                }
            }
            else if (scaleMode) {
                if (i < 8) selectedChan = i;
            }
            else {
                // Normal mode:
                // Top-row (i<8)  → select channel for display + set editStep
                // Bottom-row (i>=8) → set editStep to that step
                // Clicking the same step again → cancel editStep (toggle off)
                if (editStep == i) {
                    editStep = -1;  // unlock
                } else {
                    editStep = i;
                    if (i < 8) selectedChan = i;
                }
            }
        }

        // ============================================================
        // 3. STEP EDITING via editStep lock
        //
        // When editStep >= 0, all 8 sliders continuously write to
        // stepCV[ch][editStep] for all 8 channels every frame.
        // User workflow:
        //   1. Click a step button → it locks (editStep = that step)
        //      Step LED pulses brighter to confirm
        //   2. Drag any/all sliders to set CV for that step
        //   3. Click the same step button again → unlocks (editStep = -1)
        //      OR click another step button to jump to that step
        // ============================================================
        if (noMode && editStep >= 0) {
            for (int ch = 0; ch < 8; ch++) {
                stepCV[ch][editStep] = params[SLIDER_PARAMS + ch].getValue();
            }
        }

        // ============================================================
        // 4. LENGTH MODE — sliders set per-channel length
        // Only updates if slider has moved > LENGTH_DEADBAND from the
        // position it was at when LENGTH was latched. This prevents
        // the length from jumping the instant LENGTH is clicked.
        // ============================================================
        if (lengthMode) {
            for (int ch = 0; ch < 8; ch++) {
                float sv = params[SLIDER_PARAMS + ch].getValue();
                float snap = lengthSliderSnapshot[ch];
                if (snap < 0.f || std::abs(sv - snap) > LENGTH_DEADBAND) {
                    int newLen = clamp((int)(sv / 5.0f * 16.f) + 1, 1, 16);
                    seqLength[ch] = newLen;
                    if (seqPos[ch] >= seqLength[ch]) seqPos[ch] = 0;
                }
            }
        }

        // ============================================================
        // 5. SCALE MODE — selected channel's slider sets its scale
        // Only updates if slider has moved > SCALE_DEADBAND from snapshot.
        // ============================================================
        if (scaleMode) {
            float sv   = params[SLIDER_PARAMS + selectedChan].getValue();
            float snap = scaleSliderSnapshot;
            if (snap < 0.f || std::abs(sv - snap) > SCALE_DEADBAND) {
                scaleIndex[selectedChan] = clamp((int)(sv / 5.0f * 15.5f), 0, 15);
            }
        }

        // ============================================================
        // 6. RESET / HOLD
        // ============================================================
        int  clkMode  = (int)params[CLK_SWITCH_PARAM].getValue();
        bool holdHigh = (clkMode == 1) && (inputs[RESET_INPUT].getVoltage() > 1.0f);

        if (clkMode == 0 || clkMode == 2) {
            if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
                for (int ch = 0; ch < 8; ch++) seqPos[ch] = 0;
                divCount = 0;
            }
        }

        // ============================================================
        // 7. CLOCK / ADVANCE
        // ============================================================
        bool clocked = false;
        if (clkMode == 1) {
            if (!holdHigh) {
                float cv = inputs[CLOCK_INPUT].getVoltage();
                for (int ch = 0; ch < 8; ch++) {
                    int len = seqLength[ch];
                    seqPos[ch] = clamp((int)(cv / 10.f * (float)len), 0, len-1);
                }
            }
        } else {
            if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                int div = (int)params[DIVIDE_PARAM].getValue();
                if (++divCount >= div) { divCount = 0; clocked = true; }
            }
        }

        if (clocked) {
            for (int ch = 0; ch < 8; ch++) {
                if (frozen[ch]) continue;
                // Live record: write slider → current step BEFORE advancing
                // Only for channels with live record enabled (right-click toggle)
                if (noMode && liveRecord[ch]) {
                    stepCV[ch][seqPos[ch]] = params[SLIDER_PARAMS + ch].getValue();
                }
                advanceChannel(ch);
            }
        }

        // ============================================================
        // 8. OUTPUTS
        // ============================================================
        float att = params[ATTENUATE_PARAM].getValue();
        float off = params[OFFSET_PARAM].getValue();

        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];

            float ledBright = (ch == selectedChan) ? 1.0f : 0.15f;
            if (chanMuted[ch] || stepMuted[ch][pos]) ledBright = 0.0f;
            lights[CHANNEL_LIGHTS + ch].setBrightness(ledBright);

            if (chanMuted[ch] || stepMuted[ch][pos]) {
                outputs[CV_OUTPUTS + ch].setVoltage(0.f);
                continue;
            }

            float v = stepCV[ch][pos];

            if (scaleIndex[ch] > 0 && scaleIndex[ch] < 15)
                v = quantizeVoltage(v / 5.0f, scaleIndex[ch]) * 5.0f;

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

        // ============================================================
        // 9. STEP LIGHTS
        // STEP_LIGHTS (red)    = playhead position for selectedChan
        // EDIT_LIGHTS (yellow) = editStep locked for editing
        // Separate overlapping lights so colours never conflict.
        // ============================================================
        for (int i = 0; i < 16; i++) {
            bool isCurrent = (i == seqPos[selectedChan]);
            bool isEdit    = (i == editStep);
            bool isMuted   = stepMuted[selectedChan][i];
            bool inLen     = (i < seqLength[selectedChan]);

            // Red playhead light
            float rb;
            if      (isCurrent)  rb = 1.0f;
            else if (!inLen)     rb = 0.0f;
            else if (isMuted)    rb = 0.04f;
            else                 rb = 0.12f;
            lights[STEP_LIGHTS + i].setBrightness(rb);

            // Yellow edit-lock light (independent)
            lights[EDIT_LIGHTS + i].setBrightness(isEdit ? 0.8f : 0.0f);
        }
    }

    // ============================================================
    // JSON PERSISTENCE
    // ============================================================
    json_t* dataToJson() override {
        json_t* root = json_object();
        auto arrF = [&](const char* key, std::function<void(json_t*)> fn) {
            json_t* a = json_array(); fn(a);
            json_object_set_new(root, key, a);
        };
        arrF("stepCV", [&](json_t* a){
            for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++)
                json_array_append_new(a,json_real(stepCV[ch][s]));
        });
        arrF("seqLength",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_integer(seqLength[ch]));
        });
        arrF("direction",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_integer(direction[ch]));
        });
        arrF("pendDir",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_integer(pendDir[ch]));
        });
        arrF("scaleIndex",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_integer(scaleIndex[ch]));
        });
        arrF("chanMuted",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_boolean(chanMuted[ch]));
        });
        arrF("stepMuted",[&](json_t* a){
            for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++)
                json_array_append_new(a,json_boolean(stepMuted[ch][s]));
        });
        arrF("stepSmooth",[&](json_t* a){
            for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++)
                json_array_append_new(a,json_boolean(stepSmooth[ch][s]));
        });
        arrF("frozen",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_boolean(frozen[ch]));
        });
        arrF("liveRecord",[&](json_t* a){
            for(int ch=0;ch<8;ch++) json_array_append_new(a,json_boolean(liveRecord[ch]));
        });
        json_object_set_new(root,"selectedChan",json_integer(selectedChan));
        return root;
    }

    void dataFromJson(json_t* root) override {
        auto getI=[&](const char* k,int idx){
            json_t* a=json_object_get(root,k);
            return a?(int)json_integer_value(json_array_get(a,idx)):0;
        };
        auto getF=[&](const char* k,int idx){
            json_t* a=json_object_get(root,k);
            return a?(float)json_real_value(json_array_get(a,idx)):0.f;
        };
        auto getB=[&](const char* k,int idx){
            json_t* a=json_object_get(root,k);
            return a?json_boolean_value(json_array_get(a,idx)):false;
        };
        int idx=0;
        for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepCV[ch][s]=getF("stepCV",idx++);
        for(int ch=0;ch<8;ch++) seqLength[ch] =getI("seqLength",ch);
        for(int ch=0;ch<8;ch++) direction[ch]  =getI("direction",ch);
        for(int ch=0;ch<8;ch++) pendDir[ch]    =getI("pendDir",ch);
        for(int ch=0;ch<8;ch++) scaleIndex[ch] =getI("scaleIndex",ch);
        for(int ch=0;ch<8;ch++) chanMuted[ch]  =getB("chanMuted",ch);
        for(int ch=0;ch<8;ch++) frozen[ch]      =getB("frozen",ch);
        for(int ch=0;ch<8;ch++) liveRecord[ch]  =getB("liveRecord",ch);
        idx=0;
        for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepMuted[ch][s] =getB("stepMuted",idx++);
        idx=0;
        for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepSmooth[ch][s]=getB("stepSmooth",idx++);
        json_t* sc=json_object_get(root,"selectedChan");
        if(sc) selectedChan=(int)json_integer_value(sc);
    }

    void onRandomize(const RandomizeEvent& e) override {
        for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++)
            stepCV[ch][s]=random::uniform()*5.f;
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for(int ch=0;ch<8;ch++){
            for(int s=0;s<16;s++){
                stepCV[ch][s]=0.f;
                stepMuted[ch][s]=false;
                stepSmooth[ch][s]=false;
            }
            seqLength[ch]=16; seqPos[ch]=0;
            direction[ch]=0;  pendDir[ch]=1;
            scaleIndex[ch]=0; chanMuted[ch]=false;
            frozen[ch]=false; glideCV[ch]=0.f;
            liveRecord[ch]=false;
        }
        selectedChan=0; prevSelectedChan=0; divCount=0; editStep=-1;
        scaleSliderSnapshot = -1.f;
        for (int ch=0; ch<8; ch++) lengthSliderSnapshot[ch] = -1.f;
    }
};

// ============================================================
// SlimFader
// ============================================================
struct SlimFader : app::ParamWidget {
    static const int TW=6, TH=60, HW=14, HH=8;
    bool  dragging=false;
    float dragStartY=0.f, dragStartVal=0.f;
    SlimFader(){ box.size=Vec(HW,TH+HH); }

    void drawLayer(const DrawArgs& args,int layer) override {
        if(layer!=1) return;
        float val=getParamQuantity()?getParamQuantity()->getScaledValue():0.f;
        float handleY=(1.f-val)*TH;
        float tx=(box.size.x-TW)*0.5f;
        // Track
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg,tx,HH*0.5f,TW,TH,3.f);
        nvgFillColor(args.vg,nvgRGB(0xb8,0xb5,0xae));
        nvgFill(args.vg);
        // Active fill
        nvgBeginPath(args.vg);
        nvgRect(args.vg,tx,HH*0.5f+handleY,TW,TH-handleY);
        nvgFillColor(args.vg,nvgRGB(0x99,0x20,0x20));
        nvgFill(args.vg);
        // Handle
        float hx=(box.size.x-HW)*0.5f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg,hx,handleY,HW,HH,2.f);
        nvgFillColor(args.vg,nvgRGB(0x30,0x30,0x30));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg,hx+2.f,handleY+HH*0.5f);
        nvgLineTo(args.vg,hx+HW-2.f,handleY+HH*0.5f);
        nvgStrokeColor(args.vg,nvgRGB(0x80,0x80,0x80));
        nvgStrokeWidth(args.vg,1.f);
        nvgStroke(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if(e.action==GLFW_PRESS&&e.button==GLFW_MOUSE_BUTTON_LEFT){
            dragging=true;
            dragStartY=e.pos.y;
            dragStartVal=getParamQuantity()?getParamQuantity()->getScaledValue():0.f;
            e.consume(this);
        }
        if(e.action==GLFW_RELEASE) dragging=false;
        ParamWidget::onButton(e);
    }
    void onDragMove(const DragMoveEvent& e) override {
        if(!dragging||!getParamQuantity()) return;
        float delta=-e.mouseDelta.y/(float)TH;
        dragStartVal=clamp(dragStartVal+delta,0.f,1.f);
        getParamQuantity()->setScaledValue(dragStartVal);
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        if(getParamQuantity()) getParamQuantity()->reset();
    }
};

// ============================================================
// Widget
// ============================================================
struct SkylineWidget : ModuleWidget {
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance,"res/Skyline.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH,0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x-2*RACK_GRID_WIDTH,0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH,RACK_GRID_HEIGHT-RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x-2*RACK_GRID_WIDTH,RACK_GRID_HEIGHT-RACK_GRID_WIDTH)));

        // scale = 300/101.6 = 2.9528 px/mm
        const float cX[8]={7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};
        const float xJack=7.00f, xSwitch=20.00f;
        const float xK1=32.0f, xK2=48.5f, xK3=65.0f;
        const float xB1=78.5f, xB2=87.0f, xB3=95.5f;
        const float yOut=22.5f, yLed=31.0f;
        const float yClk=46.0f, yKnob=53.5f, yRst=61.0f;
        const float yB1=46.0f, yB2=61.0f;
        const float ySld=70.0f, yS1=104.0f, yS2=119.0f, ySLbl=126.5f;

        for(int ch=0;ch<8;ch++){
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(cX[ch],yOut)),module,Skyline::CV_OUTPUTS+ch));
            addChild(createLightCentered<SmallLight<RedLight>>(
                mm2px(Vec(cX[ch],yLed)),module,Skyline::CHANNEL_LIGHTS+ch));
        }

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xJack,yClk)),module,Skyline::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xJack,yRst)),module,Skyline::RESET_INPUT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(xSwitch,yKnob)),module,Skyline::CLK_SWITCH_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK1,yKnob)),module,Skyline::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK2,yKnob)),module,Skyline::ATTENUATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK3,yKnob)),module,Skyline::DIVIDE_PARAM));

        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(
            mm2px(Vec(xB1,yB1)),module,Skyline::MUTE_PARAM,  Skyline::MUTE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(xB2,yB1)),module,Skyline::LENGTH_PARAM,Skyline::LENGTH_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(xB3,yB1)),module,Skyline::SHIFT_PARAM, Skyline::SHIFT_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(xB1,yB2)),module,Skyline::SCALE_PARAM, Skyline::SCALE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
            mm2px(Vec(xB2,yB2)),module,Skyline::SAVE_PARAM,  Skyline::SAVE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
            mm2px(Vec(xB3,yB2)),module,Skyline::RECALL_PARAM,Skyline::RECALL_LIGHT));

        for(int ch=0;ch<8;ch++)
            addParam(createParam<SlimFader>(
                mm2px(Vec(cX[ch]-2.37f,ySld)),module,Skyline::SLIDER_PARAMS+ch));

        for(int i=0;i<8;i++){
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(
                mm2px(Vec(cX[i],yS1)),module,
                Skyline::STEP_PARAMS+i,  Skyline::STEP_LIGHTS+i));
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(
                mm2px(Vec(cX[i],yS2)),module,
                Skyline::STEP_PARAMS+8+i,Skyline::STEP_LIGHTS+8+i));
            // Yellow edit-lock lights overlaid on same positions
            addChild(createLightCentered<SmallLight<YellowLight>>(
                mm2px(Vec(cX[i],yS1)),module,Skyline::EDIT_LIGHTS+i));
            addChild(createLightCentered<SmallLight<YellowLight>>(
                mm2px(Vec(cX[i],yS2)),module,Skyline::EDIT_LIGHTS+8+i));
        }

        // Panel labels
        struct PanelLabel : widget::Widget {
            std::string text; float fontSize; NVGcolor color;
            PanelLabel(Vec c,std::string t,float sz,NVGcolor col)
                :text(t),fontSize(sz),color(col){
                box.pos=c.minus(Vec(40,8)); box.size=Vec(80,16);
            }
            void draw(const DrawArgs& args) override {
                nvgFontSize(args.vg,fontSize);
                nvgTextAlign(args.vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg,color);
                nvgText(args.vg,box.size.x*.5f,box.size.y*.5f,text.c_str(),nullptr);
            }
        };
        auto lbl=[&](float x,float y,const char* t,float sz=8.f,
                     NVGcolor c=nvgRGB(0x33,0x33,0x33)){
            addChild(new PanelLabel(mm2px(Vec(x,y)),t,sz,c));
        };

        lbl(50.8f, 5.0f,"SKYLINE",             12.f,nvgRGB(0x11,0x11,0x11));
        lbl(50.8f, 9.5f,"8 CHANNEL CV SEQUENCER",7.f,nvgRGB(0x77,0x77,0x77));
        for(int i=0;i<8;i++) lbl(cX[i],14.5f,std::to_string(i+1).c_str(),9.f);
        lbl(xJack,38.5f,"CLK/CV", 7.5f);
        lbl(xJack,66.5f,"RST/HLD",7.5f);
        lbl(xK1,  38.5f,"OFFSET", 7.5f);
        lbl(xK2,  38.5f,"ATTEN",  7.5f);
        lbl(xK3,  38.5f,"DIVIDE", 7.5f);
        lbl(xB1,  38.5f,"MUTE",   7.5f);
        lbl(xB2,  38.5f,"LEN",    7.5f);
        lbl(xB3,  38.5f,"SHIFT",  7.5f);
        lbl(xB1,  54.5f,"SCALE",  7.5f);
        lbl(xB2,  54.5f,"SAVE",   7.5f);
        lbl(xB3,  54.5f,"RECALL", 7.5f);
        const char* fn[8]={"CLEAR","SMOOTH","RND","FREEZE","FWD","REV","PEND","RNDSEQ"};
        for(int i=0;i<8;i++) lbl(cX[i],ySLbl,fn[i],7.f);
    }

    // Right-click menu: live record toggles per channel
    void appendContextMenu(Menu* menu) override {
        Skyline* m = dynamic_cast<Skyline*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Live Record (slider follows clock)"));
        for (int ch = 0; ch < 8; ch++) {
            menu->addChild(createBoolMenuItem(
                string::f("Channel %d", ch+1),
                "", [=]{ return m->liveRecord[ch]; },
                [=](bool v){ m->liveRecord[ch] = v; }
            ));
        }
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");
