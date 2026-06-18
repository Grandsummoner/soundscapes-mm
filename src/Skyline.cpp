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
        DIVIDE_PARAM, ATTENUATE_PARAM, OFFSET_PARAM, CLK_SWITCH_PARAM,
        MUTE_PARAM, LENGTH_PARAM, SHIFT_PARAM, SCALE_PARAM, SAVE_PARAM, RECALL_PARAM,
        ENUMS(SLIDER_PARAMS, 8),
        ENUMS(STEP_PARAMS, 16),
        NUM_PARAMS
    };
    enum InputIds  { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { ENUMS(CV_OUTPUTS, 8), NUM_OUTPUTS };
    enum LightIds  {
        // RGB lights — each uses 3 consecutive IDs (R, G, B)
        ENUMS(STEP_LIGHTS,     16*3),  // tiny LED above button = playhead (RGB)
        ENUMS(BUTTON_LIGHTS,   16*3),  // button LED = mode colour (RGB)
        ENUMS(CHANNEL_LIGHTS,   8*3),  // channel output LED (RGB)
        ENUMS(EDIT_RING_LIGHTS,  8),   // yellow glow ring (single float, NanoVG)
        ENUMS(MUTE_LIGHT,   3),        // latch button LEDs (RGB)
        ENUMS(LENGTH_LIGHT, 3),
        ENUMS(SHIFT_LIGHT,  3),
        ENUMS(SCALE_LIGHT,  3),
        ENUMS(SAVE_LIGHT,   3),
        ENUMS(RECALL_LIGHT, 3),
        NUM_LIGHTS
    };

    // ── RGB helper ───────────────────────────────────────────
    void setRGB(int baseId, float r, float g, float b) {
        lights[baseId + 0].setBrightness(r);
        lights[baseId + 1].setBrightness(g);
        lights[baseId + 2].setBrightness(b);
    }
    void clearRGB(int baseId) { setRGB(baseId, 0.f, 0.f, 0.f); }

    // ── Save/Recall animation ─────────────────────────────────
    // SAVE: progressive amber fill buttons 0→savedSlot over 0.8s
    // RECALL: show saved slots in amber, recalled slot in cyan briefly
    struct SaveAnimation {
        bool  active    = false;
        int   slot      = -1;      // which slot was saved/recalled
        float timer     = 0.f;
        float duration  = 0.8f;    // total fill time
        bool  isRecall  = false;
    };
    SaveAnimation saveAnim;

    // ── Sequencer state ──────────────────────────────────────
    float stepCV[8][16]     = {};
    int   seqLength[8]      = {16,16,16,16,16,16,16,16};
    int   seqPos[8]         = {};
    bool  stepMuted[8][16]  = {};
    bool  chanMuted[8]      = {};
    bool  stepSmooth[8][16] = {};
    int   direction[8]      = {};
    int   pendDir[8]        = {1,1,1,1,1,1,1,1};
    int   scaleIndex[8]     = {};
    bool  frozen[8]         = {};
    int   selectedChan      = 0;

    // editChan: which channel is in edit mode. ALWAYS >= 0 — one channel
    // is always selected. Default = 0 (channel 1). Cannot be cleared to -1.
    // Switch by double-clicking a DIFFERENT channel button.
    int   editChan          = 0;
    // editStep: which step slider writes to.
    // When editStepLocked=false: follows seqPos[editChan] (OG live record)
    // When editStepLocked=true:  locked to a specific step (step button clicked)
    int   editStep          = 0;
    bool  editStepLocked    = false;

    // Glow animation phase for edit ring
    float glowPhase         = 0.f;

    // Live recording per channel (right-click opt-in)
    // Live recording is now always-on per channel (no opt-in needed) —
    // each slider independently records to its own channel's sequence.

    // Deadband snapshots
    float lengthSliderSnapshot[8] = {-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f};
    float scaleSliderSnapshot     = -1.f;
    int   prevSelectedChan        = 0;
    static constexpr float LENGTH_DEADBAND = 0.15f;
    static constexpr float SCALE_DEADBAND  = 0.15f;

    // Presets
    float presetCV[16][8][16] = {};
    int   presetLen[16][8]    = {};
    int   presetScale[16][8]  = {};
    int   presetDir[16][8]    = {};
    bool  presetValid[16]     = {};

    // Mode flags
    bool muteMode=false, lengthMode=false, shiftMode=false;
    bool scaleMode=false, saveMode=false,  recallMode=false;
    bool prevMuteMode=false, prevLengthMode=false, prevShiftMode=false;
    bool prevScaleMode=false, prevSaveMode=false,  prevRecallMode=false;

    dsp::SchmittTrigger clockTrig, resetTrig, stepTrig[16];
    int   divCount  = 0;
    float glideCV[8]= {};
    float prevSlider[8]    = {0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f}; // slider delta tracking
    int   lastSeqPos[8]    = {};       // step position captured before advance
    float stepHoldTimer    = 0.f;      // hold window after clock edge
    static constexpr float STEP_HOLD_WINDOW = 0.08f; // 80ms

    Skyline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(DIVIDE_PARAM,    1.f,16.f,1.f,"Clock Divide");
        getParamQuantity(DIVIDE_PARAM)->snapEnabled = true;
        configParam(ATTENUATE_PARAM, 0.f, 1.f,1.f,"Attenuate");
        configParam(OFFSET_PARAM,   -5.f, 5.f,0.f,"Offset"," V");
        configSwitch(CLK_SWITCH_PARAM,0.f,2.f,0.f,"Clock Mode",{"CLK","CV","SLAVE"});
        configButton(MUTE_PARAM,  "Mute");
        configButton(LENGTH_PARAM,"Length");
        configButton(SHIFT_PARAM, "Shift");
        configButton(SCALE_PARAM, "Scale");
        configButton(SAVE_PARAM,  "Save");
        configButton(RECALL_PARAM,"Recall");
        for (int i=0;i<8;i++) {
            configParam(SLIDER_PARAMS+i,0.f,4.f,0.f,string::f("Ch %d Slider",i+1)," V");
            configOutput(CV_OUTPUTS+i,string::f("Ch %d CV",i+1));
        }
        for (int i=0;i<16;i++)
            configButton(STEP_PARAMS+i,string::f("Step %d",i+1));
        configInput(CLOCK_INPUT,"Clock");
        configInput(RESET_INPUT,"Reset / Hold");
    }

    void advanceChannel(int ch) {
        int len = seqLength[ch];
        switch (direction[ch]) {
            case 0: seqPos[ch]=(seqPos[ch]+1)%len; break;
            case 1: seqPos[ch]=(seqPos[ch]-1+len)%len; break;
            case 2:
                seqPos[ch]+=pendDir[ch];
                if(seqPos[ch]>=len-1){seqPos[ch]=len-1;pendDir[ch]=-1;}
                if(seqPos[ch]<=0)   {seqPos[ch]=0;     pendDir[ch]= 1;}
                break;
            case 3: seqPos[ch]=(int)(random::uniform()*len); break;
        }
    }

    // Preset divide storage (one per slot)
    float presetDivide[16] = {1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f,
                               1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f};

    void savePreset(int slot) {
        for(int ch=0;ch<8;ch++){
            for(int s=0;s<16;s++) presetCV[slot][ch][s]=stepCV[ch][s];
            presetLen[slot][ch]  =seqLength[ch];
            presetScale[slot][ch]=scaleIndex[ch];
            presetDir[slot][ch]  =direction[ch];
        }
        presetDivide[slot] = params[DIVIDE_PARAM].getValue();
        presetValid[slot]  = true;
    }
    void recallPreset(int slot) {
        if(!presetValid[slot]) return;
        for(int ch=0;ch<8;ch++){
            for(int s=0;s<16;s++) stepCV[ch][s]=presetCV[slot][ch][s];
            seqLength[ch] =presetLen[slot][ch];
            scaleIndex[ch]=presetScale[slot][ch];
            direction[ch] =presetDir[slot][ch];
            if(seqPos[ch] >= seqLength[ch])
                seqPos[ch] = seqLength[ch] - 1;
        }
        params[DIVIDE_PARAM].setValue(presetDivide[slot]);
        divCount = 0; // reset divide counter so new division starts cleanly
    }

    void process(const ProcessArgs& args) override {

        // ============================================================
        // 1. MODE LATCHES — exclusive, one active at a time
        // Read raw param values first
        // ============================================================
        bool rawMute   = params[MUTE_PARAM].getValue()   > 0.5f;
        bool rawLength = params[LENGTH_PARAM].getValue()  > 0.5f;
        bool rawShift  = params[SHIFT_PARAM].getValue()   > 0.5f;
        bool rawScale  = params[SCALE_PARAM].getValue()   > 0.5f;
        bool rawSave   = params[SAVE_PARAM].getValue()    > 0.5f;
        bool rawRecall = params[RECALL_PARAM].getValue()  > 0.5f;

        // Detect which mode was just activated this frame
        bool muteTrig   = rawMute   && !prevMuteMode;
        bool lengthTrig = rawLength && !prevLengthMode;
        bool shiftTrig  = rawShift  && !prevShiftMode;
        bool scaleTrig  = rawScale  && !prevScaleMode;
        bool saveTrig   = rawSave   && !prevSaveMode;
        bool recallTrig = rawRecall && !prevRecallMode;

        // If a NEW mode just activated, force all others off (exclusive)
        if (muteTrig || lengthTrig || shiftTrig || scaleTrig || saveTrig || recallTrig) {
            if (!muteTrig)   params[MUTE_PARAM].setValue(0.f);
            if (!lengthTrig) params[LENGTH_PARAM].setValue(0.f);
            if (!shiftTrig)  params[SHIFT_PARAM].setValue(0.f);
            if (!scaleTrig)  params[SCALE_PARAM].setValue(0.f);
            if (!saveTrig)   params[SAVE_PARAM].setValue(0.f);
            if (!recallTrig) params[RECALL_PARAM].setValue(0.f);
        }

        // Re-read after exclusive enforcement
        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        saveMode   = params[SAVE_PARAM].getValue()    > 0.5f;
        recallMode = params[RECALL_PARAM].getValue()  > 0.5f;

        // Entering a mode suspends step editing (editStep stays for return)
        // editChan is NEVER cleared — always one channel is active

        // LENGTH snapshot: capture editChan's slider on latch
        // Re-snap if editChan changes while LENGTH is active
        bool lengthChanChanged = lengthMode && (editChan != prevSelectedChan);
        if ((!prevLengthMode && lengthMode) || lengthChanChanged)
            lengthSliderSnapshot[editChan] = params[SLIDER_PARAMS + editChan].getValue();
        if (prevLengthMode && !lengthMode)
            for(int ch=0;ch<8;ch++) lengthSliderSnapshot[ch] = -1.f;

        // SCALE snapshot (re-snap on channel change too)
        bool scaleChChanged = scaleMode && (editChan != prevSelectedChan);
        if ((!prevScaleMode && scaleMode) || scaleChChanged)
            scaleSliderSnapshot = params[SLIDER_PARAMS+editChan].getValue();
        if (prevScaleMode && !scaleMode) scaleSliderSnapshot = -1.f;
        prevSelectedChan = editChan;

        bool anyReleased = (prevMuteMode&&!muteMode)||(prevLengthMode&&!lengthMode)||
                           (prevShiftMode&&!shiftMode)||(prevScaleMode&&!scaleMode)||
                           (prevSaveMode&&!saveMode)||(prevRecallMode&&!recallMode);
        if (anyReleased) for(int i=0;i<16;i++) stepTrig[i].reset();

        prevMuteMode=muteMode; prevLengthMode=lengthMode; prevShiftMode=shiftMode;
        prevScaleMode=scaleMode; prevSaveMode=saveMode; prevRecallMode=recallMode;

        // Latch button LEDs — each its own colour
        if (muteMode)   setRGB(MUTE_LIGHT,   0.6f,0.f,1.f);   // Purple
        else            clearRGB(MUTE_LIGHT);
        if (lengthMode) setRGB(LENGTH_LIGHT, 0.f,1.f,0.f);    // Green
        else            clearRGB(LENGTH_LIGHT);
        if (shiftMode)  setRGB(SHIFT_LIGHT,  1.f,1.f,1.f);    // White
        else            clearRGB(SHIFT_LIGHT);
        if (scaleMode)  setRGB(SCALE_LIGHT,  0.f,0.4f,1.f);   // Blue
        else            clearRGB(SCALE_LIGHT);
        if (saveMode)   setRGB(SAVE_LIGHT,   1.f,0.6f,0.f);   // Amber
        else            clearRGB(SAVE_LIGHT);
        if (recallMode) setRGB(RECALL_LIGHT, 0.f,1.f,1.f);    // Cyan
        else            clearRGB(RECALL_LIGHT);

        bool noMode = !muteMode&&!lengthMode&&!shiftMode&&!scaleMode&&!saveMode&&!recallMode;

        // selectedChan always mirrors editChan so display is consistent
        selectedChan = editChan;

        // ============================================================
        // 2. STEP BUTTON COMBOS
        // ============================================================
        for (int i = 0; i < 16; i++) {
            if (!stepTrig[i].process(params[STEP_PARAMS+i].getValue())) continue;

            if (saveMode) {
                savePreset(i);
                params[SAVE_PARAM].setValue(0.f);
                saveAnim = {true, i, 0.f, 0.8f, false};
            }
            else if (recallMode) {
                recallPreset(i);
                params[RECALL_PARAM].setValue(0.f);
                saveAnim = {true, i, 0.f, 0.4f, true};
            }
            else if (muteMode) {
                if (i < 8) {
                    // Toggle channel mute — do NOT change editChan
                    chanMuted[i] = !chanMuted[i];
                }
                else {
                    // Bottom-row: toggle step mute for editChan (steps 8-15)
                    stepMuted[editChan][i] = !stepMuted[editChan][i];
                }
            }
            else if (lengthMode) {
                // Top-row in LENGTH mode: viewing only, no editChan change
                // editChan is the channel whose length is being set
                // Switch channels via double-click as usual
            }
            else if (shiftMode) {
                // Top-row in SHIFT mode: no editChan change
                // All shift operations apply to editChan
                if (i >= 8) {
                    switch (i) {
                        case 8:  for(int s=0;s<16;s++) stepCV[editChan][s]=0.f; break;
                        case 9:  stepSmooth[editChan][seqPos[editChan]]=
                                     !stepSmooth[editChan][seqPos[editChan]]; break;
                        case 10: {
                            for(int s=0;s<seqLength[editChan];s++)
                                stepCV[editChan][s]=random::uniform()*4.f;
                            break;
                        }
                        case 11: frozen[editChan]=!frozen[editChan]; break;
                        case 12: direction[editChan]=0; break;
                        case 13: direction[editChan]=1; break;
                        case 14: direction[editChan]=2; break;
                        case 15: direction[editChan]=3; break;
                        default: break;
                    }
                }
            }
            else if (scaleMode) {
                // Top-row in SCALE mode: no editChan change
                // Scale applies to editChan via slider
            }
            else {
                // Normal mode — step buttons lock/unlock editStep
                // Click a step = lock to that step (red LED stays on it)
                // Click same step again = unlock (follow playhead = OG live record)
                if (editStepLocked && editStep == i) {
                    // Same step: unlock, go back to live record
                    editStepLocked = false;
                    editStep = seqPos[editChan];
                } else {
                    // Different step or not locked: lock to this step
                    editStep = i;
                    editStepLocked = true;
                }
            }
        }

        // ============================================================
        // 3. LIVE RECORDING — all 8 sliders independently, no selection needed
        //
        // Each channel's slider ALWAYS live-records to that channel's own
        // sequence, regardless of which channel is glowing (editChan).
        // Slider 1 → channel 1, slider 2 → channel 2, simultaneously.
        //
        // editChan/editStep/editStepLocked are now used ONLY for precision
        // step-locking on the currently glowing channel — if that channel
        // has a step locked, ITS slider writes to the locked step instead
        // of the live playhead. All OTHER channels always live-record to
        // their current playing step (using lastSeqPos during hold window
        // for accurate boundary timing).
        // ============================================================
        if (noMode) {
            for (int ch = 0; ch < 8; ch++) {
                float sv = params[SLIDER_PARAMS + ch].getValue();
                if (std::abs(sv - prevSlider[ch]) > 0.0001f) {
                    int targetStep;
                    if (ch == editChan && editStepLocked) {
                        targetStep = editStep;             // precision lock
                    } else if (stepHoldTimer > 0.f) {
                        targetStep = lastSeqPos[ch];        // step that just played
                    } else {
                        targetStep = seqPos[ch];            // current step
                    }
                    stepCV[ch][targetStep] = sv;
                    prevSlider[ch] = sv;
                }
            }
        } else {
            for (int ch = 0; ch < 8; ch++)
                prevSlider[ch] = params[SLIDER_PARAMS + ch].getValue();
        }

        // ============================================================
        // 4. LENGTH MODE — slider of editChan sets that channel's length
        // Only editChan is affected — other channels independent.
        // ============================================================
        if (lengthMode) {
            float sv   = params[SLIDER_PARAMS + editChan].getValue();
            float snap = lengthSliderSnapshot[editChan];
            if (snap < 0.f || std::abs(sv - snap) > LENGTH_DEADBAND) {
                int newLen = clamp((int)std::round(sv / 4.0f * 16.f), 1, 16);
                seqLength[editChan] = newLen;
                if (seqPos[editChan] >= seqLength[editChan]) seqPos[editChan] = 0;
            }
        }

        // ============================================================
        // 5. SCALE MODE — selected channel's slider sets scale (deadband)
        // ============================================================
        if (scaleMode) {
            float sv   = params[SLIDER_PARAMS+editChan].getValue();
            float snap = scaleSliderSnapshot;
            if (snap < 0.f || std::abs(sv-snap) > SCALE_DEADBAND)
                scaleIndex[editChan] = clamp((int)(sv / 4.0f * 15.5f), 0, 15);
        }

        // ============================================================
        // 6. RESET / HOLD
        // ============================================================
        int  clkMode  = (int)params[CLK_SWITCH_PARAM].getValue();
        bool holdHigh = (clkMode==1) && (inputs[RESET_INPUT].getVoltage()>1.0f);
        if (clkMode==0 || clkMode==2) {
            if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
                for(int ch=0;ch<8;ch++) seqPos[ch]=0;
                divCount=0;
            }
        }

        // ============================================================
        // 7. CLOCK / ADVANCE
        // ============================================================
        bool clocked = false;
        if (clkMode==1) {
            if (!holdHigh) {
                float cv=inputs[CLOCK_INPUT].getVoltage();
                for(int ch=0;ch<8;ch++){
                    int len=seqLength[ch];
                    seqPos[ch]=clamp((int)(cv/10.f*(float)len),0,len-1);
                }
            }
        } else {
            if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                int div=(int)params[DIVIDE_PARAM].getValue();
                if(++divCount>=div){divCount=0;clocked=true;}
            }
        }

        if (clocked) {
            for(int ch=0;ch<8;ch++){
                if(frozen[ch]) continue;
                lastSeqPos[ch] = seqPos[ch]; // capture BEFORE advance
                advanceChannel(ch);
                if (ch == editChan && !editStepLocked)
                    editStep = seqPos[editChan];
            }
            stepHoldTimer = STEP_HOLD_WINDOW; // start hold window
        }
        if (stepHoldTimer > 0.f) stepHoldTimer -= args.sampleTime;

        // ============================================================
        // 8. OUTPUTS
        // ============================================================
        float att=params[ATTENUATE_PARAM].getValue();
        float off=params[OFFSET_PARAM].getValue();

        for(int ch=0;ch<8;ch++){
            int pos=seqPos[ch];
            bool muted = chanMuted[ch]||stepMuted[ch][pos];
            bool frz   = frozen[ch];
            float bright = (ch==editChan)?1.0f:0.3f;
            if (muted)
                setRGB(CHANNEL_LIGHTS+ch*3, 0.6f*bright, 0.f, 1.f*bright); // Purple
            else if (frz)
                setRGB(CHANNEL_LIGHTS+ch*3, bright, bright, bright);        // White
            else
                setRGB(CHANNEL_LIGHTS+ch*3, bright, 0.f, 0.f);             // Red

            if(muted){
                outputs[CV_OUTPUTS+ch].setVoltage(0.f);
                continue;
            }
            float v=stepCV[ch][pos];
            if(scaleIndex[ch]>0&&scaleIndex[ch]<15)
                v=quantizeVoltage(v/4.0f,scaleIndex[ch])*4.0f;
            if(stepSmooth[ch][pos]){
                float rate=1.0f/(args.sampleRate*0.05f);
                glideCV[ch]+=(v-glideCV[ch])*rate;
                v=glideCV[ch];
            } else {
                glideCV[ch]=v;
            }
            v=clamp(v*att+off,-5.f,10.f);
            outputs[CV_OUTPUTS+ch].setVoltage(v);
        }

        // ============================================================
        // 9. STEP LIGHTS
        // ============================================================
        // 9. STEP LIGHTS, BUTTON LIGHTS, CHANNEL LIGHTS — full RGB
        // ============================================================

        // ── Save animation tick ──
        if (saveAnim.active) {
            saveAnim.timer += args.sampleTime;
            if (saveAnim.timer >= saveAnim.duration) saveAnim.active = false;
        }
        float animProgress = saveAnim.active
            ? clamp(saveAnim.timer / saveAnim.duration, 0.f, 1.f)
            : -1.f;
        // How many buttons filled so far (0–16)
        int animFill = saveAnim.active
            ? (int)std::floor(animProgress * (saveAnim.slot + 1))
            : -1;

        for (int i = 0; i < 16; i++) {
            bool isCurrent = (i == seqPos[editChan]);
            bool isMuted   = stepMuted[editChan][i];
            bool inLen     = (i < seqLength[editChan]);

            // ── Button LED ──
            if (saveAnim.active && !saveAnim.isRecall) {
                // SAVE progressive fill: amber sweeps left→right up to saved slot
                if (i <= saveAnim.slot && i <= animFill)
                    setRGB(BUTTON_LIGHTS + i*3, 1.f, 0.6f, 0.f);    // Amber fill
                else
                    clearRGB(BUTTON_LIGHTS + i*3);
            }
            else if (saveMode) {
                // SAVE latched: all buttons dim amber — any can be a save slot
                // Slots with saved data are brighter
                float b = presetValid[i] ? 0.7f : 0.15f;
                setRGB(BUTTON_LIGHTS + i*3, b, b*0.6f, 0.f);        // Amber dim/bright
            }
            else if (saveAnim.active && saveAnim.isRecall) {
                // RECALL animation: recalled slot flashes cyan, others dim
                if (i == saveAnim.slot)
                    setRGB(BUTTON_LIGHTS + i*3, 0.f, 1.f, 1.f);     // Cyan bright
                else if (presetValid[i])
                    setRGB(BUTTON_LIGHTS + i*3, 0.f, 0.4f, 0.4f);   // Cyan dim
                else
                    clearRGB(BUTTON_LIGHTS + i*3);
            }
            else if (recallMode) {
                // RECALL latched: show which slots have saved data in cyan
                float b = presetValid[i] ? 0.8f : 0.05f;
                setRGB(BUTTON_LIGHTS + i*3, 0.f, b, b);              // Cyan
            }
            else if (muteMode) {
                // Purple: lit = muted
                float b = (i < 8) ? (chanMuted[i] ? 1.f : 0.f)
                                  : (stepMuted[editChan][i] ? 1.f : 0.f);
                setRGB(BUTTON_LIGHTS + i*3, 0.6f*b, 0.f, 1.f*b);   // Purple
            }
            else if (lengthMode) {
                // Green: show length of editChan
                float b = (i == seqLength[editChan]-1) ? 1.f
                        : (inLen ? 0.4f : 0.f);
                setRGB(BUTTON_LIGHTS + i*3, 0.f, b, 0.f);           // Green
            }
            else if (scaleMode) {
                // Blue: selected scale button
                float b = (i == scaleIndex[editChan]) ? 1.f : 0.f;
                setRGB(BUTTON_LIGHTS + i*3, 0.f, 0.4f*b, 1.f*b);   // Blue
            }
            else if (shiftMode) {
                // White dim: bottom-row shows accessible functions
                float b = (i >= 8) ? 0.3f : 0.f;
                setRGB(BUTTON_LIGHTS + i*3, b, b, b);               // White
            }
            else {
                // Normal: playhead = bright red, locked = medium, live target = dim
                // muted in-length = dim purple, out of length = off
                if (isCurrent) {
                    setRGB(BUTTON_LIGHTS + i*3, 1.f, 0.f, 0.f);        // playhead bright red
                } else if (editStepLocked && i == editStep) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.5f, 0.f, 0.f);       // locked step medium red
                } else if (!editStepLocked && i == seqPos[editChan]) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.3f, 0.f, 0.f);       // live target dim red
                } else if (isMuted && inLen) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.1f, 0.f, 0.15f);     // muted dim purple
                } else if (inLen) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.05f, 0.f, 0.f);      // in-length very dim
                } else {
                    clearRGB(BUTTON_LIGHTS + i*3);                      // outside length = off
                }
            }
        }

        // ── Channel LEDs: red=active, purple=muted, white=frozen ──
        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];
            bool muted  = chanMuted[ch] || stepMuted[ch][pos];
            bool frz    = frozen[ch];
            float bright = (ch == editChan) ? 1.0f : 0.3f;
            if (muted)
                setRGB(CHANNEL_LIGHTS + ch*3, 0.6f*bright, 0.f, 1.f*bright); // Purple
            else if (frz)
                setRGB(CHANNEL_LIGHTS + ch*3, bright, bright, bright);        // White
            else
                setRGB(CHANNEL_LIGHTS + ch*3, bright, 0.f, 0.f);             // Red
        }

        // ============================================================
        // 10. EDIT RING LIGHTS — slow yellow glow, always one channel
        // ============================================================
        glowPhase += args.sampleTime * 1.5f;
        if (glowPhase > 2.f * M_PI) glowPhase -= 2.f * M_PI;
        float glow = 0.45f + 0.45f * std::sin(glowPhase);
        for (int ch = 0; ch < 8; ch++)
            lights[EDIT_RING_LIGHTS + ch].setBrightness(ch == editChan ? glow : 0.f);
    }

    // ============================================================
    // JSON
    // ============================================================
    json_t* dataToJson() override {
        json_t* root=json_object();
        auto arrF=[&](const char* key,std::function<void(json_t*)> fn){
            json_t* a=json_array(); fn(a); json_object_set_new(root,key,a);
        };
        arrF("stepCV",[&](json_t* a){
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
        arrF("presetDivide",[&](json_t* a){
            for(int s=0;s<16;s++) json_array_append_new(a,json_real(presetDivide[s]));
        });
        json_object_set_new(root,"selectedChan",json_integer(selectedChan));
        json_object_set_new(root,"divideParam",json_real(params[DIVIDE_PARAM].getValue()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        auto getI=[&](const char* k,int idx){json_t* a=json_object_get(root,k);return a?(int)json_integer_value(json_array_get(a,idx)):0;};
        auto getF=[&](const char* k,int idx){json_t* a=json_object_get(root,k);return a?(float)json_real_value(json_array_get(a,idx)):0.f;};
        auto getB=[&](const char* k,int idx){json_t* a=json_object_get(root,k);return a?json_boolean_value(json_array_get(a,idx)):false;};
        int idx=0;
        for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepCV[ch][s]=getF("stepCV",idx++);
        for(int ch=0;ch<8;ch++) seqLength[ch] =getI("seqLength",ch);
        for(int ch=0;ch<8;ch++) direction[ch]  =getI("direction",ch);
        for(int ch=0;ch<8;ch++) pendDir[ch]    =getI("pendDir",ch);
        for(int ch=0;ch<8;ch++) scaleIndex[ch] =getI("scaleIndex",ch);
        for(int ch=0;ch<8;ch++) chanMuted[ch]  =getB("chanMuted",ch);
        for(int ch=0;ch<8;ch++) frozen[ch]      =getB("frozen",ch);
        for(int s=0;s<16;s++)  presetDivide[s]=getF("presetDivide",s);
        idx=0; for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepMuted[ch][s] =getB("stepMuted",idx++);
        idx=0; for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepSmooth[ch][s]=getB("stepSmooth",idx++);
        json_t* sc=json_object_get(root,"selectedChan");
        if(sc) selectedChan=(int)json_integer_value(sc);
        json_t* dv=json_object_get(root,"divideParam");
        if(dv) params[DIVIDE_PARAM].setValue((float)json_real_value(dv));
    }

    void onRandomize(const RandomizeEvent& e) override {
        for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepCV[ch][s]=random::uniform()*5.f;
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for(int ch=0;ch<8;ch++){
            for(int s=0;s<16;s++){stepCV[ch][s]=0.f;stepMuted[ch][s]=false;stepSmooth[ch][s]=false;}
            seqLength[ch]=16;seqPos[ch]=0;direction[ch]=0;pendDir[ch]=1;
            scaleIndex[ch]=0;chanMuted[ch]=false;frozen[ch]=false;
            glideCV[ch]=0.f;
        }
        selectedChan=0;prevSelectedChan=0;divCount=0;
        editChan=0; editStep=0; editStepLocked=false; glowPhase=0.f;
        scaleSliderSnapshot=-1.f;
        for(int ch=0;ch<8;ch++) lengthSliderSnapshot[ch]=-1.f;
    }
};

// ============================================================
// SlimFader
// ============================================================
struct SlimFader : app::ParamWidget {
    static const int TW=6,TH=60,HW=14,HH=8;
    bool dragging=false; float dragStartY=0.f,dragStartVal=0.f;
    SlimFader(){box.size=Vec(HW,TH+HH);}
    void drawLayer(const DrawArgs& args,int layer) override {
        if(layer!=1) return;
        float val=getParamQuantity()?getParamQuantity()->getScaledValue():0.f;
        float handleY=(1.f-val)*TH, tx=(box.size.x-TW)*0.5f;
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,tx,HH*0.5f,TW,TH,3.f);
        nvgFillColor(args.vg,nvgRGB(0xb8,0xb5,0xae)); nvgFill(args.vg);
        nvgBeginPath(args.vg); nvgRect(args.vg,tx,HH*0.5f+handleY,TW,TH-handleY);
        nvgFillColor(args.vg,nvgRGB(0x99,0x20,0x20)); nvgFill(args.vg);
        float hx=(box.size.x-HW)*0.5f;
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,hx,handleY,HW,HH,2.f);
        nvgFillColor(args.vg,nvgRGB(0x30,0x30,0x30)); nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg,hx+2.f,handleY+HH*0.5f); nvgLineTo(args.vg,hx+HW-2.f,handleY+HH*0.5f);
        nvgStrokeColor(args.vg,nvgRGB(0x80,0x80,0x80)); nvgStrokeWidth(args.vg,1.f); nvgStroke(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if(e.action==GLFW_PRESS&&e.button==GLFW_MOUSE_BUTTON_LEFT){
            dragging=true; dragStartY=e.pos.y;
            dragStartVal=getParamQuantity()?getParamQuantity()->getScaledValue():0.f;
            e.consume(this);
        }
        if(e.action==GLFW_RELEASE) dragging=false;
        ParamWidget::onButton(e);
    }
    void onDragMove(const DragMoveEvent& e) override {
        if(!dragging||!getParamQuantity()) return;
        // Divide by TH/2 instead of TH for 2× sensitivity
        // User can hold Ctrl for fine control (standard VCV behaviour)
        float sensitivity = (APP->window->getMods() & RACK_MOD_CTRL) ? (float)TH*4 : (float)TH/2;
        float delta = -e.mouseDelta.y / sensitivity;
        dragStartVal = clamp(dragStartVal + delta, 0.f, 1.f);
        getParamQuantity()->setScaledValue(dragStartVal);
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        if(getParamQuantity()) getParamQuantity()->reset();
    }
};

// ============================================================
// EditRingLight — yellow glowing ring drawn around a channel LED
// ============================================================
struct EditRingLight : widget::Widget {
    int     lightId  = 0;
    Module* skyModule = nullptr;   // explicit pointer — widget::Widget has no module member

    EditRingLight() { box.size = Vec(22, 22); }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        if (!skyModule) return;
        float brightness = skyModule->lights[lightId].getBrightness();
        if (brightness <= 0.001f) return;

        Vec centre = box.size.div(2.f);
        float r = 9.5f;

        // Outer glow (soft, wide)
        NVGpaint glow = nvgRadialGradient(args.vg,
            centre.x, centre.y, r * 0.7f, r * 1.6f,
            nvgRGBAf(1.f, 0.85f, 0.f, brightness * 0.55f),
            nvgRGBAf(1.f, 0.85f, 0.f, 0.f));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, centre.x, centre.y, r * 1.6f);
        nvgFillPaint(args.vg, glow);
        nvgFill(args.vg);

        // Inner ring stroke
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, centre.x, centre.y, r);
        nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.85f, 0.f, brightness * 0.9f));
        nvgStrokeWidth(args.vg, 1.8f);
        nvgStroke(args.vg);
    }
};

// ============================================================
// ChannelStepButton — subclasses VCVLightButton, intercepts
// double-click to toggle editChan.
// KEY: onDoubleClick only fires if onButton consumes the event.
// We call the parent onButton first (so single-click still works
// via stepTrig in process()), then consume to enable double-click.
// ============================================================
struct ChannelStepButton : VCVLightButton<MediumSimpleLight<RedGreenBlueLight>> {
    int chanIndex = -1;

    void onButton(const ButtonEvent& e) override {
        // Call parent so single-click param behaviour is preserved
        VCVLightButton<MediumSimpleLight<RedGreenBlueLight>>::onButton(e);
        // Must consume left-press to receive onDoubleClick
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
            e.consume(this);
    }

    void onDoubleClick(const DoubleClickEvent& e) override {
        if (!module) return;
        Skyline* m = dynamic_cast<Skyline*>(module);
        if (!m) return;
        // Only switch if this is a DIFFERENT channel from current.
        // Double-clicking the currently glowing channel is ignored —
        // there must always be exactly one channel in edit.
        if (m->editChan != chanIndex) {
            m->editChan       = chanIndex;
            m->editStep       = 0;
            m->editStepLocked = false;
            m->selectedChan   = chanIndex;
            // Sync to current slider so first wiggle is detected correctly
            m->prevSlider[chanIndex] =
                m->params[Skyline::SLIDER_PARAMS + chanIndex].getValue();
        }
        // Same channel double-click: do nothing
        e.consume(this);
    }
};

// ============================================================
// Widget
// ============================================================
struct SkylineWidget : ModuleWidget {
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance,"res/Skyline.svg")));

        // No screws — clean panel aesthetic

        const float cX[8]={7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};
        const float xJack=7.00f,xSwitch=20.00f;
        const float xK1=32.0f,xK2=48.5f,xK3=65.0f;
        const float xB1=78.5f,xB2=87.0f,xB3=95.5f;
        const float yOut=22.5f,yLed=31.0f;
        const float yClk=46.0f,yKnob=53.5f,yRst=61.0f;
        const float yB1=46.0f,yB2=61.0f;
        const float ySld=70.0f,yS1=104.0f,yS2=119.0f,ySLbl=126.5f;

        // ── CV outputs + channel LEDs + edit ring lights ─────
        for(int ch=0;ch<8;ch++){
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(cX[ch],yOut)),module,Skyline::CV_OUTPUTS+ch));

            // Edit ring (drawn behind channel LED)
            auto* ring = new EditRingLight;
            ring->skyModule = module;
            ring->lightId   = Skyline::EDIT_RING_LIGHTS + ch;
            ring->box.pos   = mm2px(Vec(cX[ch],yLed)).minus(ring->box.size.div(2.f));
            addChild(ring);

            // Channel LED — RGB (red/purple/white states)
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[ch],yLed)),module,Skyline::CHANNEL_LIGHTS+ch*3));
        }

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xJack,yClk)),module,Skyline::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xJack,yRst)),module,Skyline::RESET_INPUT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(xSwitch,yKnob)),module,Skyline::CLK_SWITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK1,yKnob)),module,Skyline::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK2,yKnob)),module,Skyline::ATTENUATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK3,yKnob)),module,Skyline::DIVIDE_PARAM));

        // Latch buttons — RGB for per-mode colour
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedGreenBlueLight>>>(
            mm2px(Vec(xB1,yB1)),module,Skyline::MUTE_PARAM,  Skyline::MUTE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedGreenBlueLight>>>(
            mm2px(Vec(xB2,yB1)),module,Skyline::LENGTH_PARAM,Skyline::LENGTH_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedGreenBlueLight>>>(
            mm2px(Vec(xB3,yB1)),module,Skyline::SHIFT_PARAM, Skyline::SHIFT_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedGreenBlueLight>>>(
            mm2px(Vec(xB1,yB2)),module,Skyline::SCALE_PARAM, Skyline::SCALE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedGreenBlueLight>>>(
            mm2px(Vec(xB2,yB2)),module,Skyline::SAVE_PARAM,  Skyline::SAVE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedGreenBlueLight>>>(
            mm2px(Vec(xB3,yB2)),module,Skyline::RECALL_PARAM,Skyline::RECALL_LIGHT));

        for(int ch=0;ch<8;ch++)
            addParam(createParam<SlimFader>(
                mm2px(Vec(cX[ch]-2.37f,ySld)),module,Skyline::SLIDER_PARAMS+ch));

        // ── Step buttons ─────────────────────────────────────
        for(int i=0;i<8;i++){
            // Top-row: ChannelStepButton with RGB button light
            auto* csb = createLightParamCentered<ChannelStepButton>(
                mm2px(Vec(cX[i],yS1)), module,
                Skyline::STEP_PARAMS+i, Skyline::BUTTON_LIGHTS+i*3);
            csb->chanIndex = i;
            addParam(csb);

            // Bottom-row: RGB button light
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedGreenBlueLight>>>(
                mm2px(Vec(cX[i],yS2)),module,
                Skyline::STEP_PARAMS+8+i, Skyline::BUTTON_LIGHTS+(8+i)*3));
        }

        // Panel labels
        struct PanelLabel : widget::Widget {
            std::string text; float fontSize; NVGcolor color;
            PanelLabel(Vec c,std::string t,float sz,NVGcolor col)
                :text(t),fontSize(sz),color(col){box.pos=c.minus(Vec(40,8));box.size=Vec(80,16);}
            void draw(const DrawArgs& args) override {
                nvgFontSize(args.vg, fontSize);
                nvgFontFaceId(args.vg, APP->window->uiFont->handle);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, color);
                // Draw text twice offset by 0.3px for bold effect
                nvgText(args.vg, box.size.x*.5f,     box.size.y*.5f,     text.c_str(), nullptr);
                nvgText(args.vg, box.size.x*.5f+0.3f,box.size.y*.5f,     text.c_str(), nullptr);
            }
        };
        auto lbl=[&](float x,float y,const char* t,float sz=8.f,
                     NVGcolor c=nvgRGB(0x33,0x33,0x33)){
            addChild(new PanelLabel(mm2px(Vec(x,y)),t,sz,c));
        };
        lbl(50.8f,5.0f,"SKYLINE",12.f,nvgRGB(0x11,0x11,0x11));
        lbl(50.8f,9.5f,"8 CHANNEL CV SEQUENCER",7.f,nvgRGB(0x77,0x77,0x77));
        for(int i=0;i<8;i++) lbl(cX[i],14.5f,std::to_string(i+1).c_str(),9.f);
        lbl(xJack,38.5f,"CLK/CV",7.5f); lbl(xJack,66.5f,"RST/HLD",7.5f);
        lbl(xK1,38.5f,"OFFSET",7.5f); lbl(xK2,38.5f,"ATTEN",7.5f); lbl(xK3,38.5f,"DIVIDE",7.5f);
        lbl(xB1,38.5f,"MUTE",7.5f); lbl(xB2,38.5f,"LEN",7.5f); lbl(xB3,38.5f,"SHIFT",7.5f);
        lbl(xB1,54.5f,"SCALE",7.5f); lbl(xB2,54.5f,"SAVE",7.5f); lbl(xB3,54.5f,"RECALL",7.5f);
        const char* fn[8]={"CLEAR","SMOOTH","RND","FREEZE","FWD","REV","PEND","RNDSEQ"};
        for(int i=0;i<8;i++) lbl(cX[i],ySLbl,fn[i],7.f);
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");
