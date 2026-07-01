#include "plugin.hpp"
#include <cmath>

// ============================================================
// Scales
// ============================================================
static const float SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},
    {0,1,5,7,10,0,1,5,7,10,0,1},
    {0,2,4,7,9,0,2,4,7,9,0,2},
    {0,3,5,7,10,0,3,5,7,10,0,3},
    {0,3,5,6,7,10,0,3,5,6,7,10},
    {0,1,3,4,6,8,10,0,1,3,4,6},
    {0,2,4,5,6,8,10,0,2,4,5,6},
    {0,1,3,5,7,8,10,0,1,3,5,7},
    {0,2,3,5,7,8,10,0,2,3,5,7},
    {0,2,3,5,7,9,10,0,2,3,5,7},
    {0,2,4,5,7,9,10,0,2,4,5,7},
    {0,1,4,5,7,8,11,0,1,4,5,7},
    {0,1,4,5,7,8,11,0,1,4,5,7},
    {0,2,4,5,7,9,11,0,2,4,5,7},
    {0,2,4,6,7,9,11,0,2,4,6,7},
    {0,1,2,3,4,5,6,7,8,9,10,11},
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

// Exponential gear ratio — noon (k=0.5) = 1x unity, range 1/16x to 16x
static float expRatioFromKnob(float k) {
    return std::pow(2.f, (k - 0.5f) * 8.f);
}

// Euclidean rhythm generator — places n hits as evenly as possible in len steps
static void buildEuclidean(bool* pat, int len, int hits, int rotation) {
    for (int i = 0; i < len; i++) pat[i] = false;
    if (hits <= 0) return;
    hits = clamp(hits, 0, len);
    // Bjorklund/Euclidean distribution
    int bucket = 0;
    for (int i = 0; i < len; i++) {
        bucket += hits;
        if (bucket >= len) { bucket -= len; pat[i] = true; }
    }
    // rotate
    rotation = ((rotation % len) + len) % len;
    bool tmp[16];
    for (int i = 0; i < len; i++) tmp[i] = pat[(i + len - rotation) % len];
    for (int i = 0; i < len; i++) pat[i] = tmp[i];
}

// LFO harmonic ratios per channel (ch0=master)
static const float LFO_RATIOS[8] = {1.f, 2.f, 3.f, 4.f, 0.5f, 1.f/3.f, 0.25f, 1.f/6.f};

// ============================================================
struct Soundscape : Module {
// ============================================================
    enum ParamIds {
        ENGINE_MODE_PARAM,   // 3-way: INT=0 / EXT=1 / MOD=2
        KNOB1_PARAM,         // SYNC/CLOCK/DENSITY-RATE
        KNOB2_PARAM,         // OFFSET/ROTATION-PHASE
        KNOB3_PARAM,         // RHYTHM/DIVISION-WAVEFORM
        MUTE_PARAM, LENGTH_PARAM, SHIFT_PARAM, SCALE_PARAM, SAVE_PARAM, RECALL_PARAM,
        ENUMS(SLIDER_PARAMS,   8),
        ENUMS(STEP_PARAMS,    16),
        ENUMS(CHMODE_PARAMS,   8),  // cycles C=0/P=1/G=2/L=3
        NUM_PARAMS
    };
    enum InputIds  { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { ENUMS(CV_OUTPUTS, 8), CLOCK_OUT, NUM_OUTPUTS };
    enum LightIds  {
        ENUMS(STEP_LIGHTS,      16*3),
        ENUMS(BUTTON_LIGHTS,    16*3),
        ENUMS(CHANNEL_LIGHTS,    8*3),
        ENUMS(EDIT_RING_LIGHTS,  8),
        ENUMS(MUTE_LIGHT,   3),
        ENUMS(LENGTH_LIGHT, 3),
        ENUMS(SHIFT_LIGHT,  3),
        ENUMS(SCALE_LIGHT,  3),
        ENUMS(SAVE_LIGHT,   3),
        ENUMS(RECALL_LIGHT, 3),
        NUM_LIGHTS
    };

    enum EngineMode { ENGINE_INT=0, ENGINE_EXT=1, ENGINE_MOD=2 };
    enum ChanMode   { CH_CV=0, CH_PITCH=1, CH_GATE=2, CH_LFO=3 };

    // ---- helpers ----
    void setRGB(int id, float r, float g, float b) {
        lights[id+0].setBrightness(r);
        lights[id+1].setBrightness(g);
        lights[id+2].setBrightness(b);
    }
    void clearRGB(int id) { setRGB(id, 0.f, 0.f, 0.f); }

    // ---- save/recall animation ----
    struct SaveAnimation {
        bool  active=false; int slot=-1;
        float timer=0.f, duration=0.8f; bool isRecall=false;
        void trigger(int sl, float dur, bool rec){ active=true; slot=sl; timer=0.f; duration=dur; isRecall=rec; }
    };
    SaveAnimation saveAnim;

    // ---- 7-seg banner notification ----
    // Fires on ENGINE_MODE switch; all 8 displays show a coloured message
    // for NOTIFY_DURATION seconds then fade back to per-channel glyphs.
    static constexpr float NOTIFY_DURATION = 1.5f;
    static constexpr float NOTIFY_FADE     = 0.3f;
    float  notifyTimer   = 0.f;   // counts UP while active
    int    notifyMode    = -1;    // which mode triggered the notification
    int    prevEngineMode = -1;   // to detect mode changes

    // Banner characters per mode (8 chars, one per display):
    // INT: "CLK OUT " (green)
    // EXT: "CLK  IN " (amber/yellow)
    // MOD: "MOD  ON " (purple)
    static const char* notifyString(int mode) {
        if (mode == ENGINE_INT) return "CLK OUT ";
        if (mode == ENGINE_EXT) return "CLK  IN ";
        return "MOD  ON ";
    }

    // ---- sequencer state (ported from Skyline) ----
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
    int   editChan         = 0;
    int   globalStep       = -1;
    float glowPhase        = 0.f;

    float lengthSliderSnapshot[8] = {-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f};
    float scaleSliderSnapshot     = -1.f;
    int   prevSelectedChan        = 0;
    static constexpr float LENGTH_DEADBAND = 0.15f;
    static constexpr float SCALE_DEADBAND  = 0.15f;

    // presets store full CV memory + metadata
    float presetCV[16][8][16] = {};
    int   presetLen[16][8]    = {};
    int   presetScale[16][8]  = {};
    int   presetDir[16][8]    = {};
    bool  presetValid[16]     = {};

    bool muteMode=false,lengthMode=false,shiftMode=false;
    bool scaleMode=false,saveMode=false,recallMode=false;
    bool prevMuteMode=false,prevLengthMode=false,prevShiftMode=false;
    bool prevScaleMode=false,prevSaveMode=false,prevRecallMode=false;

    dsp::SchmittTrigger clockTrig, resetTrig, stepTrig[16];
    float glideCV[8]       = {};
    float prevSlider[8]    = {};
    int   lastSeqPos[8]    = {};
    float stepHoldTimer[8] = {};
    static constexpr float STEP_HOLD_WINDOW = 0.08f;
    float timeSinceLastClock = 0.f;
    float lastClockPeriod    = 0.5f;   // default = 120 BPM quarter-note period

    // ---- INT/EXT clock state ----
    float internalPhase  = 0.f;
    float extGearPhase   = 0.f;
    float extOffsetHeld  = 0.f;
    bool  extClockActive = false;   // true if a real clock edge arrived recently
    float extTimeoutTimer= 0.f;    // hard-stop timer for EXT with no clock

    // ---- stored BPM (persists across mode switches for MOD fallback) ----
    float storedBPM = 120.f;

    // ---- MOD state ----
    // G channels: Euclidean gate patterns (rebuilt when knobs change)
    bool  eucPat[8][16]    = {};
    float eucPhase[8]      = {};   // per-channel clock phase within MOD
    int   prevEucHits[8]   = {-1,-1,-1,-1,-1,-1,-1,-1};
    int   prevEucRot[8]    = {-1,-1,-1,-1,-1,-1,-1,-1};
    // L channels: LFO state
    float lfoPhase[8]      = {};
    float chTweakTimer[8]  = {};

    // ---- LFO output cache (written in process, read in draw) ----
    float lfoOut[8] = {};

    // ---- dim animation for inactive channels on mode switch ----
    float dimTimer = 0.f;
    static constexpr float DIM_FADE = 0.5f;

    Soundscape() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configSwitch(ENGINE_MODE_PARAM, 0.f, 2.f, 0.f, "Engine Mode", {"INT","EXT","MOD"});
        configParam(KNOB1_PARAM, 0.f, 1.f, 0.5f, "Sync / Density / Rate");
        configParam(KNOB2_PARAM, 0.f, 1.f, 0.5f, "Offset / Rotation / Phase");
        configParam(KNOB3_PARAM, 0.f, 1.f, 0.5f, "Rhythm / Division / Waveform");

        configButton(MUTE_PARAM,  "Mute");
        configButton(LENGTH_PARAM,"Length");
        configButton(SHIFT_PARAM, "Shift");
        configButton(SCALE_PARAM, "Scale");
        configButton(SAVE_PARAM,  "Save");
        configButton(RECALL_PARAM,"Recall");

        for (int i = 0; i < 8; i++) {
            // Fader stored as 0-1 normalised position; voltage derived at output time
            configParam(SLIDER_PARAMS + i, 0.f, 1.f, 0.f,
                string::f("Ch %d Fader", i+1));
            configOutput(CV_OUTPUTS + i, string::f("Ch %d Out", i+1));
            configSwitch(CHMODE_PARAMS + i, 0.f, 3.f, 0.f,
                string::f("Ch %d Mode", i+1), {"CV","Pitch","Gate","LFO"});
        }
        for (int i = 0; i < 16; i++)
            configButton(STEP_PARAMS + i, string::f("Step %d", i+1));

        configOutput(CLOCK_OUT, "Clock Out (INT mode only; acts as Clock In in EXT/MOD)");
        configInput(CLOCK_INPUT, "Clock / CV In (CLK OUT in INT mode)");
        configInput(RESET_INPUT, "Reset / Hold");
    }

    // ---- sequencer advance ----
    void advanceChannel(int ch) {
        int len = seqLength[ch];
        switch (direction[ch]) {
            case 0: seqPos[ch] = (seqPos[ch] + 1) % len; break;
            case 1: seqPos[ch] = (seqPos[ch] - 1 + len) % len; break;
            case 2:
                seqPos[ch] += pendDir[ch];
                if (seqPos[ch] >= len-1) { seqPos[ch]=len-1; pendDir[ch]=-1; }
                if (seqPos[ch] <= 0)     { seqPos[ch]=0;     pendDir[ch]=1; }
                break;
            case 3: seqPos[ch] = (int)(random::uniform() * len); break;
        }
    }

    // ---- presets ----
    void savePreset(int slot) {
        for (int ch=0;ch<8;ch++) {
            for (int s=0;s<16;s++) presetCV[slot][ch][s]=stepCV[ch][s];
            presetLen[slot][ch]=seqLength[ch];
            presetScale[slot][ch]=scaleIndex[ch];
            presetDir[slot][ch]=direction[ch];
        }
        presetValid[slot]=true;
    }
    void recallPreset(int slot) {
        if (!presetValid[slot]) return;
        for (int ch=0;ch<8;ch++) {
            for (int s=0;s<16;s++) stepCV[ch][s]=presetCV[slot][ch][s];
            seqLength[ch]=presetLen[slot][ch];
            scaleIndex[ch]=presetScale[slot][ch];
            direction[ch]=presetDir[slot][ch];
            if (seqPos[ch]>=seqLength[ch]) seqPos[ch]=seqLength[ch]-1;
        }
        for (int ch=0;ch<8;ch++) { eucPhase[ch]=0.f; lfoPhase[ch]=0.f; }
        internalPhase=0.f; extGearPhase=0.f;
    }

    // ---- output voltage from normalised 0-1 fader position + channel mode ----
    // Called every frame so output immediately reflects any state change.
    float computeOutput(int ch, float k2offset) {
        int chm = (int)params[CHMODE_PARAMS+ch].getValue();
        int eng = (int)params[ENGINE_MODE_PARAM].getValue();

        // Inactive combinations: C/P only in INT/EXT; G/L only in MOD
        bool cpActive = (eng==ENGINE_INT || eng==ENGINE_EXT) && (chm==CH_CV || chm==CH_PITCH);
        bool glActive = (eng==ENGINE_MOD) && (chm==CH_GATE || chm==CH_LFO);
        if (!cpActive && !glActive) return 0.f;

        int pos = seqPos[ch];
        bool muted = chanMuted[ch] || stepMuted[ch][pos];
        if (muted) return 0.f;

        if (chm == CH_LFO) {
            // LFO: amplitude from fader (0-1 → 0-5V bipolar half-swing, full = ±5V)
            float amp = params[SLIDER_PARAMS+ch].getValue() * 5.f;
            return clamp(lfoOut[ch] * amp, -5.f, 5.f);
        }

        if (chm == CH_GATE) {
            // MOD gate: driven by Euclidean pattern, output 0 or 10V
            return eucPat[ch][pos] ? 10.f : 0.f;
        }

        // C or P: read from stepCV (normalised 0-1)
        float raw = stepCV[ch][pos];

        float v;
        if (chm == CH_CV) {
            v = raw * 10.f;  // 0 to +10V
        } else {
            // Pitch: -1V to +1V (2 octaves), then quantise
            v = raw * 2.f - 1.f;
            if (scaleIndex[ch] > 0)
                v = quantizeVoltage(v, scaleIndex[ch]);
        }

        // Apply offset (INT: live; EXT: sample-held)
        v += k2offset;

        // Glide
        if (stepSmooth[ch][pos]) {
            float glideTime = std::max(lastClockPeriod * 0.9f, 0.05f);
            float rate = 1.f / (args_sampleRate * glideTime);
            glideCV[ch] += (v - glideCV[ch]) * rate;
            v = glideCV[ch];
        } else {
            glideCV[ch] = v;
        }

        return clamp(v, -5.f, 10.f);
    }

    // sampleRate stored so computeOutput can use it outside process signature
    float args_sampleRate = 44100.f;

    void process(const ProcessArgs& args) override {
        args_sampleRate = args.sampleRate;
        int engineMode = (int)params[ENGINE_MODE_PARAM].getValue();
        float k1 = params[KNOB1_PARAM].getValue();
        float k2 = params[KNOB2_PARAM].getValue();
        float k3 = params[KNOB3_PARAM].getValue();

        // -------------------------------------------------------
        // 1. Detect engine mode change → trigger banner + dim anim
        // -------------------------------------------------------
        if (engineMode != prevEngineMode) {
            notifyTimer  = 0.f;
            notifyMode   = engineMode;
            dimTimer     = 0.f;
            prevEngineMode = engineMode;
        }
        notifyTimer += args.sampleTime;
        if (dimTimer < DIM_FADE + NOTIFY_DURATION + NOTIFY_FADE)
            dimTimer += args.sampleTime;

        // -------------------------------------------------------
        // 2. CLK/CV jack — output in INT, input in EXT/MOD
        // -------------------------------------------------------
        // In INT, jack outputs a 10ms pulse per step tick (set below).
        // Dynamic tooltip is handled via configInput label already.

        // -------------------------------------------------------
        // 3. Reset / Hold
        // -------------------------------------------------------
        bool holding = inputs[RESET_INPUT].getVoltage() > 2.f;
        if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
            for (int ch=0;ch<8;ch++) { seqPos[ch]=0; eucPhase[ch]=0.f; lfoPhase[ch]=0.f; }
            internalPhase=0.f; extGearPhase=0.f;
        }

        // -------------------------------------------------------
        // 4. Latch-button submode logic (ported from Skyline)
        // -------------------------------------------------------
        bool rawMute   = params[MUTE_PARAM].getValue()   > 0.5f;
        bool rawLength = params[LENGTH_PARAM].getValue()  > 0.5f;
        bool rawShift  = params[SHIFT_PARAM].getValue()   > 0.5f;
        bool rawScale  = params[SCALE_PARAM].getValue()   > 0.5f;
        bool rawSave   = params[SAVE_PARAM].getValue()    > 0.5f;
        bool rawRecall = params[RECALL_PARAM].getValue()  > 0.5f;

        bool muteTrig  = rawMute   && !prevMuteMode;
        bool lenTrig   = rawLength && !prevLengthMode;
        bool shiftTrig = rawShift  && !prevShiftMode;
        bool scaleTrig = rawScale  && !prevScaleMode;
        bool saveTrig  = rawSave   && !prevSaveMode;
        bool recTrig   = rawRecall && !prevRecallMode;

        if (muteTrig||lenTrig||shiftTrig||scaleTrig||saveTrig||recTrig) {
            if (!muteTrig)  params[MUTE_PARAM].setValue(0.f);
            if (!lenTrig)   params[LENGTH_PARAM].setValue(0.f);
            if (!shiftTrig) params[SHIFT_PARAM].setValue(0.f);
            if (!scaleTrig) params[SCALE_PARAM].setValue(0.f);
            if (!saveTrig)  params[SAVE_PARAM].setValue(0.f);
            if (!recTrig)   params[RECALL_PARAM].setValue(0.f);
        }

        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        saveMode   = params[SAVE_PARAM].getValue()    > 0.5f;
        recallMode = params[RECALL_PARAM].getValue()  > 0.5f;

        bool lengthChanChanged = lengthMode && (editChan != prevSelectedChan);
        if ((!prevLengthMode && lengthMode) || lengthChanChanged)
            lengthSliderSnapshot[editChan] = params[SLIDER_PARAMS+editChan].getValue();
        if (prevLengthMode && !lengthMode)
            for (int ch=0;ch<8;ch++) lengthSliderSnapshot[ch]=-1.f;

        bool scaleChChanged = scaleMode && (editChan != prevSelectedChan);
        if ((!prevScaleMode && scaleMode) || scaleChChanged)
            scaleSliderSnapshot = params[SLIDER_PARAMS+editChan].getValue();
        if (prevScaleMode && !scaleMode) scaleSliderSnapshot=-1.f;
        prevSelectedChan = editChan;

        bool anyReleased = (prevMuteMode&&!muteMode)||(prevLengthMode&&!lengthMode)||
                           (prevShiftMode&&!shiftMode)||(prevScaleMode&&!scaleMode)||
                           (prevSaveMode&&!saveMode)||(prevRecallMode&&!recallMode);
        if (anyReleased) for (int i=0;i<16;i++) stepTrig[i].reset();

        prevMuteMode=muteMode; prevLengthMode=lengthMode; prevShiftMode=shiftMode;
        prevScaleMode=scaleMode; prevSaveMode=saveMode; prevRecallMode=recallMode;

        if (muteMode)   setRGB(MUTE_LIGHT,   0.6f,0.f,1.f); else clearRGB(MUTE_LIGHT);
        if (lengthMode) setRGB(LENGTH_LIGHT, 0.f,1.f,0.f);  else clearRGB(LENGTH_LIGHT);
        if (shiftMode)  setRGB(SHIFT_LIGHT,  1.f,1.f,1.f);  else clearRGB(SHIFT_LIGHT);
        if (scaleMode)  setRGB(SCALE_LIGHT,  0.f,0.4f,1.f); else clearRGB(SCALE_LIGHT);
        if (saveMode)   setRGB(SAVE_LIGHT,   1.f,0.6f,0.f); else clearRGB(SAVE_LIGHT);
        if (recallMode) setRGB(RECALL_LIGHT, 0.f,1.f,1.f);  else clearRGB(RECALL_LIGHT);

        bool noMode = !muteMode&&!lengthMode&&!shiftMode&&!scaleMode&&!saveMode&&!recallMode;

        // -------------------------------------------------------
        // 5. Step-button actions
        // -------------------------------------------------------
        for (int i=0;i<16;i++) {
            if (!stepTrig[i].process(params[STEP_PARAMS+i].getValue())) continue;
            if (saveMode) {
                savePreset(i);
                params[SAVE_PARAM].setValue(0.f);
                saveAnim.trigger(i, 0.8f, false);
            }
            else if (recallMode) {
                recallPreset(i);
                params[RECALL_PARAM].setValue(0.f);
                saveAnim.trigger(i, 0.4f, true);
            }
            else if (muteMode) {
                if (i<8) { editChan=i; chanMuted[i]=!chanMuted[i]; }
                else stepMuted[editChan][i]=!stepMuted[editChan][i];
            }
            else if (lengthMode) { if (i<8) editChan=i; }
            else if (shiftMode) {
                if (i<8) { editChan=i; }
                else {
                    switch(i) {
                        case 8:  for(int s=0;s<16;s++) stepCV[editChan][s]=0.f; break;
                        case 9:  stepSmooth[editChan][seqPos[editChan]]=!stepSmooth[editChan][seqPos[editChan]]; break;
                        case 10: for(int s=0;s<seqLength[editChan];s++) stepCV[editChan][s]=random::uniform(); break;
                        case 11: frozen[editChan]=!frozen[editChan]; break;
                        case 12: direction[editChan]=0; break;
                        case 13: direction[editChan]=1; break;
                        case 14: direction[editChan]=2; break;
                        case 15: direction[editChan]=3; break;
                        default: break;
                    }
                }
            }
            else if (scaleMode) { if (i<8) editChan=i; }
            else { globalStep=(globalStep==i)?-1:i; }
        }

        // -------------------------------------------------------
        // 6. Fader recording (INT/EXT only; normalised 0-1)
        // -------------------------------------------------------
        if (noMode && engineMode != ENGINE_MOD) {
            for (int ch=0;ch<8;ch++) {
                float sv = params[SLIDER_PARAMS+ch].getValue();
                if (std::abs(sv - prevSlider[ch]) > 0.0001f) {
                    int targetStep;
                    if (globalStep >= 0) targetStep = globalStep;
                    else if (stepHoldTimer[ch] > 0.f) targetStep = lastSeqPos[ch];
                    else targetStep = seqPos[ch];
                    stepCV[ch][targetStep] = sv;
                    prevSlider[ch] = sv;
                    chTweakTimer[ch] = 0.5f;
                }
            }
        } else {
            for (int ch=0;ch<8;ch++) prevSlider[ch]=params[SLIDER_PARAMS+ch].getValue();
        }
        for (int ch=0;ch<8;ch++) {
            if (chTweakTimer[ch] > 0.f) chTweakTimer[ch] -= args.sampleTime;
            if (stepHoldTimer[ch] > 0.f) stepHoldTimer[ch] -= args.sampleTime;
        }

        // -------------------------------------------------------
        // 7. Length / Scale fader override
        // -------------------------------------------------------
        if (lengthMode) {
            float sv = params[SLIDER_PARAMS+editChan].getValue();
            float snap = lengthSliderSnapshot[editChan];
            if (snap<0.f || std::abs(sv-snap)>LENGTH_DEADBAND) {
                int newLen = clamp((int)std::round(sv*16.f),1,16);
                seqLength[editChan] = newLen;
                if (seqPos[editChan]>=seqLength[editChan]) seqPos[editChan]=0;
            }
        }
        if (scaleMode) {
            float sv = params[SLIDER_PARAMS+editChan].getValue();
            float snap = scaleSliderSnapshot;
            if (snap<0.f || std::abs(sv-snap)>SCALE_DEADBAND)
                scaleIndex[editChan] = clamp((int)(sv*15.5f),0,15);
        }

        // -------------------------------------------------------
        // 8. Clock processing
        // -------------------------------------------------------
        timeSinceLastClock += args.sampleTime;
        bool realClockEdge = false;

        if (engineMode == ENGINE_INT) {
            // Derive BPM from knob1 so noon=120; store for MOD fallback
            float bpm = 20.f + k1 * 220.f;   // 20–240 BPM, noon~120
            storedBPM = bpm;
            float stepPeriod = 60.f / (bpm * 4.f);  // 16th-note grid
            internalPhase += args.sampleTime;
            if (!holding && internalPhase >= stepPeriod) {
                internalPhase -= stepPeriod;
                // Advance all C/P channels
                for (int ch=0;ch<8;ch++) {
                    int chm = (int)params[CHMODE_PARAMS+ch].getValue();
                    if (chm==CH_CV || chm==CH_PITCH) {
                        if (!frozen[ch]) {
                            lastSeqPos[ch]=seqPos[ch];
                            advanceChannel(ch);
                            stepHoldTimer[ch]=STEP_HOLD_WINDOW;
                        }
                    }
                }
                // Clock-out: in INT mode CLK/CV jack conceptually outputs the master
                // clock. Implemented by adding CLOCK_OUT to OutputIds (same panel position
                // as the CLOCK_INPUT jack, switched by engine mode). Pulse goes high for
                // one sample per step tick then returns low.
                realClockEdge = true;
            }
            // Drive CLOCK_OUT high for this sample, low otherwise
            outputs[CLOCK_OUT].setVoltage(realClockEdge ? 10.f : 0.f);
        }
        else if (engineMode == ENGINE_EXT) {
            outputs[CLOCK_OUT].setVoltage(0.f);  // clock out only active in INT
            realClockEdge = clockTrig.process(inputs[CLOCK_INPUT].getVoltage());
            if (realClockEdge) {
                if (timeSinceLastClock>0.001f && timeSinceLastClock<10.f)
                    lastClockPeriod = timeSinceLastClock;
                timeSinceLastClock = 0.f;
                extOffsetHeld = (k2-0.5f)*10.f;
                extGearPhase  = 0.f;
                extClockActive= true;
                extTimeoutTimer=0.f;
            }
            extTimeoutTimer += args.sampleTime;
            // Hard stop if no clock for 2 seconds
            if (extTimeoutTimer < 2.f && !holding) {
                float ratio = expRatioFromKnob(k1);
                float gearPeriod = lastClockPeriod / std::max(ratio, 0.001f);
                extGearPhase += args.sampleTime;
                if (extGearPhase >= gearPeriod) {
                    extGearPhase -= gearPeriod;
                    for (int ch=0;ch<8;ch++) {
                        int chm=(int)params[CHMODE_PARAMS+ch].getValue();
                        if (chm==CH_CV||chm==CH_PITCH) {
                            if (!frozen[ch]) {
                                lastSeqPos[ch]=seqPos[ch];
                                advanceChannel(ch);
                                stepHoldTimer[ch]=STEP_HOLD_WINDOW;
                            }
                        }
                    }
                }
            }
        }
        else { // ENGINE_MOD
            outputs[CLOCK_OUT].setVoltage(0.f);  // clock out only active in INT
            // Master clock: external if connected, else storedBPM fallback
            bool clkConnected = inputs[CLOCK_INPUT].isConnected();
            float modPeriod;
            if (clkConnected) {
                realClockEdge = clockTrig.process(inputs[CLOCK_INPUT].getVoltage());
                if (realClockEdge) {
                    if (timeSinceLastClock>0.001f && timeSinceLastClock<10.f)
                        lastClockPeriod = timeSinceLastClock;
                    timeSinceLastClock = 0.f;
                }
                modPeriod = lastClockPeriod;
            } else {
                // Fallback: use storedBPM, Knob1 still controls density/rate
                float bpm = storedBPM;
                modPeriod = 60.f / (bpm * 4.f);
                internalPhase += args.sampleTime;
                if (internalPhase >= modPeriod) {
                    internalPhase -= modPeriod;
                    realClockEdge = true;
                }
            }

            if (!holding) {
                // G channels: Euclidean gate patterns
                // Knob1 = density (0-16 hits), Knob2 = rotation (0-15 steps),
                // Knob3 = clock division spread
                int globalHits = (int)std::round(k1 * 16.f);
                int globalRot  = (int)std::round(k2 * 15.f);
                // Knob3: division spread — noon=all same, up=faster, down=slower
                // Channel i gets ratio = 2^((k3-0.5)*2*(i-3.5)/7)
                for (int ch=0;ch<8;ch++) {
                    int chm=(int)params[CHMODE_PARAMS+ch].getValue();
                    if (chm==CH_GATE) {
                        // Rebuild Euclidean pattern if knobs changed
                        if (globalHits!=prevEucHits[ch] || globalRot!=prevEucRot[ch]) {
                            buildEuclidean(eucPat[ch], seqLength[ch], globalHits, globalRot);
                            prevEucHits[ch]=globalHits; prevEucRot[ch]=globalRot;
                        }
                        // Advance G channel on its own clock division
                        float divRatio = std::pow(2.f, (k3-0.5f)*2.f*((float)ch-3.5f)/7.f);
                        float chPeriod = modPeriod / std::max(divRatio, 0.001f);
                        eucPhase[ch] += args.sampleTime;
                        if (eucPhase[ch] >= chPeriod) {
                            eucPhase[ch] -= chPeriod;
                            if (!frozen[ch]) advanceChannel(ch);
                        }
                    }
                }

                // L channels: phase-synced harmonic LFOs
                // Knob1 = master rate (uses storedBPM as reference, ratio via LFO_RATIOS)
                // Knob2 = phase spread between L channels
                // Knob3 = waveform morph (0=sine, 0.25=tri, 0.5=saw+, 0.75=saw-, 1=square)
                float masterRate = storedBPM / 60.f * 0.25f; // one cycle per beat at storedBPM
                // Knob1 scales rate exponentially around that reference
                float rateScale = expRatioFromKnob(k1);
                float phaseSpread = k2; // 0=all same, 1=max spread (45deg per channel)
                int lIdx = 0;
                for (int ch=0;ch<8;ch++) {
                    int chm=(int)params[CHMODE_PARAMS+ch].getValue();
                    if (chm==CH_LFO) {
                        float chRate = masterRate * rateScale * LFO_RATIOS[ch];
                        float phOffset = phaseSpread * (float)lIdx * (1.f/8.f);
                        lfoPhase[ch] += chRate * args.sampleTime;
                        if (lfoPhase[ch] >= 1.f) lfoPhase[ch] -= 1.f;
                        float p = fmodf(lfoPhase[ch] + phOffset, 1.f);
                        // waveform morph via k3
                        float wave;
                        if (k3 < 0.25f) {
                            // sine → triangle
                            float sine = std::sin(p * 2.f * (float)M_PI);
                            float tri  = (p < 0.5f) ? (4.f*p - 1.f) : (3.f - 4.f*p);
                            wave = crossfade(sine, tri, k3/0.25f);
                        } else if (k3 < 0.5f) {
                            float tri = (p < 0.5f) ? (4.f*p-1.f) : (3.f-4.f*p);
                            float saw = 2.f*p - 1.f;
                            wave = crossfade(tri, saw, (k3-0.25f)/0.25f);
                        } else if (k3 < 0.75f) {
                            float saw  =  2.f*p - 1.f;
                            float sawN = -2.f*p + 1.f;
                            wave = crossfade(saw, sawN, (k3-0.5f)/0.25f);
                        } else {
                            float sawN = -2.f*p + 1.f;
                            float sq   = (p < 0.5f) ? 1.f : -1.f;
                            wave = crossfade(sawN, sq, (k3-0.75f)/0.25f);
                        }
                        lfoOut[ch] = wave;
                        lIdx++;
                    }
                }
            }
        }

        // -------------------------------------------------------
        // 9. Output stage — immediate, every frame
        // -------------------------------------------------------
        float k2offset = 0.f;
        if (engineMode==ENGINE_INT)      k2offset = (k2-0.5f)*10.f;
        else if (engineMode==ENGINE_EXT) k2offset = extOffsetHeld;

        for (int ch=0;ch<8;ch++) {
            float v = computeOutput(ch, k2offset);
            outputs[CV_OUTPUTS+ch].setVoltage(v);
        }

        // -------------------------------------------------------
        // 10. Channel lights
        // -------------------------------------------------------
        for (int ch=0;ch<8;ch++) {
            int chm=(int)params[CHMODE_PARAMS+ch].getValue();
            bool muted = chanMuted[ch]||stepMuted[ch][seqPos[ch]];
            bool frz   = frozen[ch];
            float bright = (ch==editChan) ? 1.f : 0.3f;
            if (muted)      setRGB(CHANNEL_LIGHTS+ch*3, 0.6f*bright, 0.f, bright);
            else if (frz)   setRGB(CHANNEL_LIGHTS+ch*3, bright, bright, bright);
            else if (chm==CH_LFO) setRGB(CHANNEL_LIGHTS+ch*3, 0.f, bright*0.5f, bright);
            else if (chm==CH_GATE) setRGB(CHANNEL_LIGHTS+ch*3, bright*0.8f, bright*0.4f, 0.f);
            else if (chm==CH_PITCH) setRGB(CHANNEL_LIGHTS+ch*3, 0.f, bright, 0.f);
            else setRGB(CHANNEL_LIGHTS+ch*3, bright, 0.f, 0.f);
        }

        // -------------------------------------------------------
        // 11. Step-button lights (INT/EXT only; MOD/LFO use different display)
        // -------------------------------------------------------
        if (saveAnim.active) {
            saveAnim.timer += args.sampleTime;
            if (saveAnim.timer >= saveAnim.duration) saveAnim.active=false;
        }
        float animProg = saveAnim.active ? clamp(saveAnim.timer/saveAnim.duration,0.f,1.f) : -1.f;
        int animFill = saveAnim.active ? (int)std::floor(animProg*(saveAnim.slot+1)) : -1;

        for (int i=0;i<16;i++) {
            bool isCur  = (i==seqPos[editChan]);
            bool isMute = stepMuted[editChan][i];
            bool inLen  = (i<seqLength[editChan]);

            if (saveAnim.active && !saveAnim.isRecall) {
                if (i<=saveAnim.slot && i<=animFill) setRGB(BUTTON_LIGHTS+i*3,1.f,0.6f,0.f);
                else clearRGB(BUTTON_LIGHTS+i*3);
            } else if (saveMode) {
                float b=presetValid[i]?0.7f:0.15f;
                setRGB(BUTTON_LIGHTS+i*3,b,b*0.6f,0.f);
            } else if (saveAnim.active && saveAnim.isRecall) {
                if (i==saveAnim.slot) setRGB(BUTTON_LIGHTS+i*3,0.f,1.f,1.f);
                else if (presetValid[i]) setRGB(BUTTON_LIGHTS+i*3,0.f,0.4f,0.4f);
                else clearRGB(BUTTON_LIGHTS+i*3);
            } else if (recallMode) {
                float b=presetValid[i]?0.8f:0.05f;
                setRGB(BUTTON_LIGHTS+i*3,0.f,b,b);
            } else if (muteMode) {
                float b=(i<8)?(chanMuted[i]?1.f:0.f):(stepMuted[editChan][i]?1.f:0.f);
                setRGB(BUTTON_LIGHTS+i*3,0.6f*b,0.f,b);
            } else if (lengthMode) {
                float b=(i==seqLength[editChan]-1)?1.f:(inLen?0.4f:0.f);
                setRGB(BUTTON_LIGHTS+i*3,0.f,b,0.f);
            } else if (scaleMode) {
                float b=(i==scaleIndex[editChan])?1.f:0.f;
                setRGB(BUTTON_LIGHTS+i*3,0.f,0.4f*b,b);
            } else if (shiftMode) {
                float b=(i>=8)?0.3f:0.f;
                setRGB(BUTTON_LIGHTS+i*3,b,b,b);
            } else {
                if (isCur) setRGB(BUTTON_LIGHTS+i*3,1.f,0.f,0.f);
                else if (globalStep==i) setRGB(BUTTON_LIGHTS+i*3,0.5f,0.f,0.f);
                else if (isMute&&inLen) setRGB(BUTTON_LIGHTS+i*3,0.1f,0.f,0.15f);
                else if (inLen) setRGB(BUTTON_LIGHTS+i*3,0.05f,0.f,0.f);
                else clearRGB(BUTTON_LIGHTS+i*3);
            }
        }

        // -------------------------------------------------------
        // 12. Edit-ring glow
        // -------------------------------------------------------
        glowPhase += args.sampleTime * 1.5f;
        if (glowPhase > 2.f*(float)M_PI) glowPhase -= 2.f*(float)M_PI;
        float glow = 0.45f + 0.45f*std::sin(glowPhase);
        for (int ch=0;ch<8;ch++)
            lights[EDIT_RING_LIGHTS+ch].setBrightness(ch==editChan?glow:0.f);
    }

    // ---- serialisation ----
    json_t* dataToJson() override {
        json_t* root=json_object();
        auto arrF=[&](const char* k,std::function<void(json_t*)> fn){
            json_t* a=json_array(); fn(a); json_object_set_new(root,k,a);
        };
        arrF("stepCV",[&](json_t* a){
            for(int c=0;c<8;c++) for(int s=0;s<16;s++) json_array_append_new(a,json_real(stepCV[c][s]));
        });
        arrF("seqLength",[&](json_t* a){for(int c=0;c<8;c++) json_array_append_new(a,json_integer(seqLength[c]));});
        arrF("direction",[&](json_t* a){for(int c=0;c<8;c++) json_array_append_new(a,json_integer(direction[c]));});
        arrF("pendDir",  [&](json_t* a){for(int c=0;c<8;c++) json_array_append_new(a,json_integer(pendDir[c]));});
        arrF("scaleIdx", [&](json_t* a){for(int c=0;c<8;c++) json_array_append_new(a,json_integer(scaleIndex[c]));});
        arrF("chanMuted",[&](json_t* a){for(int c=0;c<8;c++) json_array_append_new(a,json_boolean(chanMuted[c]));});
        arrF("frozen",   [&](json_t* a){for(int c=0;c<8;c++) json_array_append_new(a,json_boolean(frozen[c]));});
        arrF("stepMuted",[&](json_t* a){
            for(int c=0;c<8;c++) for(int s=0;s<16;s++) json_array_append_new(a,json_boolean(stepMuted[c][s]));
        });
        arrF("stepSmooth",[&](json_t* a){
            for(int c=0;c<8;c++) for(int s=0;s<16;s++) json_array_append_new(a,json_boolean(stepSmooth[c][s]));
        });
        arrF("presetValid",[&](json_t* a){for(int s=0;s<16;s++) json_array_append_new(a,json_boolean(presetValid[s]));});
        arrF("presetCV",[&](json_t* a){
            for(int sl=0;sl<16;sl++) for(int c=0;c<8;c++) for(int s=0;s<16;s++)
                json_array_append_new(a,json_real(presetCV[sl][c][s]));
        });
        json_object_set_new(root,"editChan",json_integer(editChan));
        json_object_set_new(root,"storedBPM",json_real(storedBPM));
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto gI=[&](const char* k,int i)->int{
            json_t* a=json_object_get(root,k); if(!a) return 0;
            json_t* v=json_array_get(a,i); return v?(int)json_integer_value(v):0;
        };
        auto gF=[&](const char* k,int i)->float{
            json_t* a=json_object_get(root,k); if(!a) return 0.f;
            json_t* v=json_array_get(a,i); return v?(float)json_real_value(v):0.f;
        };
        auto gB=[&](const char* k,int i)->bool{
            json_t* a=json_object_get(root,k); if(!a) return false;
            json_t* v=json_array_get(a,i); return v?json_boolean_value(v):false;
        };
        int idx=0;
        for(int c=0;c<8;c++) for(int s=0;s<16;s++) stepCV[c][s]=gF("stepCV",idx++);
        for(int c=0;c<8;c++) seqLength[c]=gI("seqLength",c);
        for(int c=0;c<8;c++) direction[c]=gI("direction",c);
        for(int c=0;c<8;c++) pendDir[c]=gI("pendDir",c);
        for(int c=0;c<8;c++) scaleIndex[c]=gI("scaleIdx",c);
        for(int c=0;c<8;c++) chanMuted[c]=gB("chanMuted",c);
        for(int c=0;c<8;c++) frozen[c]=gB("frozen",c);
        idx=0; for(int c=0;c<8;c++) for(int s=0;s<16;s++) stepMuted[c][s]=gB("stepMuted",idx++);
        idx=0; for(int c=0;c<8;c++) for(int s=0;s<16;s++) stepSmooth[c][s]=gB("stepSmooth",idx++);
        for(int s=0;s<16;s++) presetValid[s]=gB("presetValid",s);
        idx=0; for(int sl=0;sl<16;sl++) for(int c=0;c<8;c++) for(int s=0;s<16;s++)
            presetCV[sl][c][s]=gF("presetCV",idx++);
        json_t* ec=json_object_get(root,"editChan"); if(ec) editChan=(int)json_integer_value(ec);
        json_t* sb=json_object_get(root,"storedBPM"); if(sb) storedBPM=(float)json_real_value(sb);
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for(int c=0;c<8;c++){
            for(int s=0;s<16;s++){stepCV[c][s]=0.f;stepMuted[c][s]=false;stepSmooth[c][s]=false;}
            seqLength[c]=16;seqPos[c]=0;direction[c]=0;pendDir[c]=1;
            scaleIndex[c]=0;chanMuted[c]=false;frozen[c]=false;glideCV[c]=0.f;
            eucPhase[c]=0.f;lfoPhase[c]=0.f;lfoOut[c]=0.f;chTweakTimer[c]=0.f;
        }
        editChan=0;globalStep=-1;glowPhase=0.f;
        internalPhase=0.f;extGearPhase=0.f;extOffsetHeld=0.f;
        storedBPM=120.f;notifyTimer=10.f;prevEngineMode=-1;
    }
    void onRandomize(const RandomizeEvent& e) override {
        for(int c=0;c<8;c++) for(int s=0;s<16;s++) stepCV[c][s]=random::uniform();
    }
};

// ============================================================
// EditRingLight — ported verbatim from Skyline
// ============================================================
struct EditRingLight : widget::Widget {
    int     lightId=0;
    Module* mod=nullptr;
    EditRingLight(){ box.size=Vec(22,22); }
    void drawLayer(const DrawArgs& args,int layer) override {
        if(layer!=1||!mod) return;
        float br=mod->lights[lightId].getBrightness();
        if(br<=0.001f) return;
        Vec c=box.size.div(2.f); float r=9.5f;
        NVGpaint glow=nvgRadialGradient(args.vg,c.x,c.y,r*0.7f,r*1.6f,
            nvgRGBAf(0.1f,0.25f,0.6f,br*0.55f),nvgRGBAf(0.1f,0.25f,0.6f,0.f));
        nvgBeginPath(args.vg); nvgCircle(args.vg,c.x,c.y,r*1.6f);
        nvgFillPaint(args.vg,glow); nvgFill(args.vg);
        nvgBeginPath(args.vg); nvgCircle(args.vg,c.x,c.y,r);
        nvgStrokeColor(args.vg,nvgRGBAf(0.1f,0.25f,0.6f,br*0.9f));
        nvgStrokeWidth(args.vg,1.8f); nvgStroke(args.vg);
    }
};

// ============================================================
// SoundscapePort
// ============================================================
struct SoundscapePort : app::SvgPort {
    SoundscapePort(){
        setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/SoundscapeJack.svg")));
    }
};

// ============================================================
// SoundscapeSlider
// ============================================================
struct SoundscapeSlider : app::SvgSlider {
    static const int TW=6,TH=90,HW=14,HH=8,TM=6;
    bool dragging=false; float dragStartVal=0.f;
    SoundscapeSlider(){
        setBackgroundSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/SoundscapeFaderBg.svg")));
        setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/SoundscapeFaderHandle.svg")));
        background->box.size=Vec(TW,TH);
        background->box.pos=Vec((HW-TW)/2.f,0.f);
        handle->box.size=Vec(HW,HH);
        setHandlePosCentered(Vec(HW/2.f,TH-HH/2.f),Vec(HW/2.f,TM+HH/2.f));
        box.size=Vec(HW,TH+HH);
        fb->box.size=box.size; fb->setDirty();
    }
    void onButton(const ButtonEvent& e) override {
        if(e.action==GLFW_PRESS&&e.button==GLFW_MOUSE_BUTTON_LEFT){
            dragging=true; dragStartVal=0.f;
            if(getParamQuantity()){
                float val=getParamQuantity()->getValue();
                float mn=getParamQuantity()->getMinValue();
                float mx=getParamQuantity()->getMaxValue();
                if(mx>mn) dragStartVal=(val-mn)/(mx-mn);
            }
            e.consume(this);
        }
        if(e.action==GLFW_RELEASE) dragging=false;
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {
        if(e.button==GLFW_MOUSE_BUTTON_LEFT) APP->window->cursorLock();
        ParamWidget::onDragStart(e);
    }
    void onDragEnd(const DragEndEvent& e) override {
        if(e.button==GLFW_MOUSE_BUTTON_LEFT) APP->window->cursorUnlock();
        ParamWidget::onDragEnd(e);
    }
    void onDragMove(const DragMoveEvent& e) override {
        if(!dragging||!getParamQuantity()) return;
        float sens=(APP->window->getMods()&RACK_MOD_CTRL)?240.f:60.f;
        float delta=-e.mouseDelta.y/sens;
        dragStartVal=clamp(dragStartVal+delta,0.f,1.f);
        float mn=getParamQuantity()->getMinValue();
        float mx=getParamQuantity()->getMaxValue();
        getParamQuantity()->setValue(mn+dragStartVal*(mx-mn));
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        if(getParamQuantity()) getParamQuantity()->reset();
    }
};

// ============================================================
// ChModeButton — cycles C/P/G/L, shows glyph or VU bar,
// shows banner notification on mode switch.
// Uses ParamWidget's own `module` ptr (no shadowing field).
// ============================================================
struct ChModeButton : ParamWidget {
    int channelId=0;
    ChModeButton(){ box.size=Vec(24,20); }

    void onButton(const ButtonEvent& e) override {
        if(e.action==GLFW_PRESS&&e.button==GLFW_MOUSE_BUTTON_LEFT){
            if(getParamQuantity()){
                int cur=(int)getParamQuantity()->getValue();
                getParamQuantity()->setValue((cur+1)%4); // C→P→G→L→C
            }
            e.consume(this);
        }
        ParamWidget::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        // Background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,2.f);
        nvgFillColor(args.vg,nvgRGBA(0x1a,0x1a,0x1a,0xff));
        nvgFill(args.vg);

        auto* mod=dynamic_cast<Soundscape*>(module);
        if(!mod) return;

        // ---- Banner notification overrides normal glyph ----
        float nt = mod->notifyTimer;
        float totalNotify = Soundscape::NOTIFY_DURATION + Soundscape::NOTIFY_FADE;
        if(nt < totalNotify && mod->notifyMode >= 0){
            const char* banner = Soundscape::notifyString(mod->notifyMode);
            // Pick colour per mode
            NVGcolor col;
            if(mod->notifyMode==Soundscape::ENGINE_INT)
                col=nvgRGBA(0x00,0xff,0x60,0xff);  // green: CLK OUT
            else if(mod->notifyMode==Soundscape::ENGINE_EXT)
                col=nvgRGBA(0xff,0xcc,0x00,0xff);  // amber/yellow: CLK IN
            else
                col=nvgRGBA(0xcc,0x00,0xff,0xff);  // purple: MOD ON

            // Fade out during last NOTIFY_FADE seconds
            float alpha=1.f;
            if(nt > Soundscape::NOTIFY_DURATION)
                alpha=1.f-(nt-Soundscape::NOTIFY_DURATION)/Soundscape::NOTIFY_FADE;
            col.a *= alpha;

            // Each display shows one character: banner[channelId]
            char glyph[2]={banner[channelId],'\0'};
            nvgFontSize(args.vg,16.f);
            nvgTextAlign(args.vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg,col);
            nvgText(args.vg,box.size.x/2.f,box.size.y/2.f,glyph,nullptr);
            return;
        }

        // ---- VU bar while fader is being moved ----
        bool tweaking=mod->chTweakTimer[channelId]>0.f;
        if(tweaking){
            int chm=(int)mod->params[Soundscape::CHMODE_PARAMS+channelId].getValue();
            float raw=mod->params[Soundscape::SLIDER_PARAMS+channelId].getValue(); // 0-1
            float cx=box.size.x/2.f;
            float midY=box.size.y/2.f;
            // bipolar: positive = above centre, negative = below
            float level=0.f;
            if(chm==Soundscape::CH_CV)    level=raw;           // 0 to +1
            else if(chm==Soundscape::CH_PITCH) level=raw*2.f-1.f; // -1 to +1
            else level=raw;

            int totalBars=4;
            float barH=2.5f, barGap=1.f;
            for(int b=0;b<totalBars;b++){
                float thresh=(float)(b+1)/(float)totalBars;
                bool lit=(level>=0.f)?(thresh<=level):(thresh<=std::abs(level));
                if(!lit) continue;
                NVGcolor c;
                if(level>=0.f){
                    float t=(float)b/(float)(totalBars-1);
                    c=nvgRGBA((int)(t*255),(int)((1.f-t)*200),0,255);
                } else {
                    c=nvgRGBA(0,100+(int)(155.f*(float)b/(float)(totalBars-1)),255,255);
                }
                float y=(level>=0.f)?(midY-(b+1)*(barH+barGap)):(midY+b*(barH+barGap));
                nvgBeginPath(args.vg);
                nvgRect(args.vg,3,y,box.size.x-6,barH);
                nvgFillColor(args.vg,c);
                nvgFill(args.vg);
            }
            // centre line
            nvgBeginPath(args.vg);
            nvgRect(args.vg,3,midY-0.5f,box.size.x-6,1.f);
            nvgFillColor(args.vg,nvgRGBA(80,80,80,200));
            nvgFill(args.vg);
            return;
        }

        // ---- Normal glyph: C / P / G / L ----
        int engMode=(int)mod->params[Soundscape::ENGINE_MODE_PARAM].getValue();
        int chm=getParamQuantity()?(int)getParamQuantity()->getValue():0;
        const char* glyph="C";
        if(chm==Soundscape::CH_PITCH) glyph="P";
        else if(chm==Soundscape::CH_GATE) glyph="G";
        else if(chm==Soundscape::CH_LFO) glyph="L";

        // Dim inactive combinations
        bool cpActive=(engMode==Soundscape::ENGINE_INT||engMode==Soundscape::ENGINE_EXT)
                      &&(chm==Soundscape::CH_CV||chm==Soundscape::CH_PITCH);
        bool glActive=(engMode==Soundscape::ENGINE_MOD)
                      &&(chm==Soundscape::CH_GATE||chm==Soundscape::CH_LFO);
        bool active=cpActive||glActive;

        // Smooth dim: use dimTimer to animate transition
        float dimAlpha;
        float dt=mod->dimTimer;
        float totalAnim=Soundscape::NOTIFY_DURATION+Soundscape::NOTIFY_FADE+Soundscape::DIM_FADE;
        if(dt < Soundscape::NOTIFY_DURATION+Soundscape::NOTIFY_FADE){
            dimAlpha=active?1.f:0.2f; // hold during banner
        } else {
            float fadeT=clamp((dt-(Soundscape::NOTIFY_DURATION+Soundscape::NOTIFY_FADE))
                              /Soundscape::DIM_FADE,0.f,1.f);
            dimAlpha=active?crossfade(0.2f,1.f,fadeT):crossfade(1.f,0.2f,fadeT);
        }

        // Gate: flash on active gate
        if(chm==Soundscape::CH_GATE&&glActive){
            bool gateHigh=mod->eucPat[channelId][mod->seqPos[channelId]];
            dimAlpha=gateHigh?1.f:0.3f;
        }
        // LFO: subtle brightness modulation showing waveform position
        if(chm==Soundscape::CH_LFO&&glActive){
            float lv=mod->lfoOut[channelId];
            dimAlpha=0.4f+0.6f*((lv+1.f)*0.5f);
        }

        nvgFontSize(args.vg,16.f);
        nvgTextAlign(args.vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGBAf(1.f,0.62f,0.f,dimAlpha));
        nvgText(args.vg,box.size.x/2.f,box.size.y/2.f,glyph,nullptr);
    }
};

// ============================================================
// Widget
// ============================================================
struct SoundscapeWidget : ModuleWidget {
    SoundscapeWidget(Soundscape* module){
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance,"res/Soundscape.svg")));

        const float cX[8]={7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};
        const float xJack=7.00f, xSwitch=20.00f;
        const float xK1=32.0f,  xK2=48.5f,  xK3=65.0f;
        const float xB1=78.5f,  xB2=87.0f,  xB3=95.5f;
        const float yChMode=17.0f, yOut=29.0f, yLed=37.0f;
        const float yClk=46.0f, yKnob=53.5f, yRst=61.0f;
        const float yB1=46.0f,  yB2=61.0f;
        const float ySld=70.0f, yS1=104.0f, yS2=119.0f;

        // Per-channel strip
        for(int ch=0;ch<8;ch++){
            auto* cm=createParam<ChModeButton>(
                mm2px(Vec(cX[ch],yChMode)).minus(Vec(12,10)),
                module, Soundscape::CHMODE_PARAMS+ch);
            cm->channelId=ch;
            addParam(cm);

            addOutput(createOutputCentered<SoundscapePort>(
                mm2px(Vec(cX[ch],yOut)),module,Soundscape::CV_OUTPUTS+ch));

            auto* ring=new EditRingLight;
            ring->mod=module;
            ring->lightId=Soundscape::EDIT_RING_LIGHTS+ch;
            ring->box.pos=mm2px(Vec(cX[ch],yLed)).minus(ring->box.size.div(2.f));
            addChild(ring);
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[ch],yLed)),module,Soundscape::CHANNEL_LIGHTS+ch*3));
        }

        // CLK/CV: input jack in EXT/MOD, output jack in INT.
        // Both ports share the same panel position — only one is active at a time.
        addInput(createInputCentered<SoundscapePort>(
            mm2px(Vec(xJack,yClk)),module,Soundscape::CLOCK_INPUT));
        addOutput(createOutputCentered<SoundscapePort>(
            mm2px(Vec(xJack,yClk)),module,Soundscape::CLOCK_OUT));
        addInput(createInputCentered<SoundscapePort>(
            mm2px(Vec(xJack,yRst)),module,Soundscape::RESET_INPUT));

        // Engine mode switch + 3 master knobs
        addParam(createParamCentered<CKSSThree>(
            mm2px(Vec(xSwitch,yKnob)),module,Soundscape::ENGINE_MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(xK1,yKnob)),module,Soundscape::KNOB1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(xK2,yKnob)),module,Soundscape::KNOB2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(xK3,yKnob)),module,Soundscape::KNOB3_PARAM));

        // 6 latch buttons
        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB1,yB1)),module,Soundscape::MUTE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB1,yB1)),module,Soundscape::MUTE_LIGHT));
        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB2,yB1)),module,Soundscape::LENGTH_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB2,yB1)),module,Soundscape::LENGTH_LIGHT));
        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB3,yB1)),module,Soundscape::SHIFT_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB3,yB1)),module,Soundscape::SHIFT_LIGHT));
        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB1,yB2)),module,Soundscape::SCALE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB1,yB2)),module,Soundscape::SCALE_LIGHT));
        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB2,yB2)),module,Soundscape::SAVE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB2,yB2)),module,Soundscape::SAVE_LIGHT));
        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB3,yB2)),module,Soundscape::RECALL_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB3,yB2)),module,Soundscape::RECALL_LIGHT));

        // 8 faders
        for(int ch=0;ch<8;ch++)
            addParam(createParam<SoundscapeSlider>(
                mm2px(Vec(cX[ch]-2.37f,ySld)),module,Soundscape::SLIDER_PARAMS+ch));

        // 16 step buttons (2 rows of 8)
        for(int i=0;i<8;i++){
            addParam(createParamCentered<VCVButton>(mm2px(Vec(cX[i],yS1)),module,Soundscape::STEP_PARAMS+i));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(cX[i],yS1)),module,Soundscape::BUTTON_LIGHTS+i*3));
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(cX[i],yS1-5.f)),module,Soundscape::STEP_LIGHTS+i*3));
            addParam(createParamCentered<VCVButton>(mm2px(Vec(cX[i],yS2)),module,Soundscape::STEP_PARAMS+8+i));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(cX[i],yS2)),module,Soundscape::BUTTON_LIGHTS+(8+i)*3));
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(cX[i],yS2-5.f)),module,Soundscape::STEP_LIGHTS+(8+i)*3));
        }
    }
};

Model* modelSoundscape = createModel<Soundscape,SoundscapeWidget>("SoundscapeMM");
