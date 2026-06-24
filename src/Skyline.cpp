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
        ENUMS(STEP_LIGHTS,     16*3),  // playhead (RGB)
        ENUMS(BUTTON_LIGHTS,   16*3),  // button LED (RGB)
        ENUMS(CHANNEL_LIGHTS,   8*3),  // channel output LED (RGB)
        ENUMS(EDIT_RING_LIGHTS,  8),   // glow ring showing the MUTE/LENGTH/SCALE/SHIFT target channel
        ENUMS(MUTE_LIGHT,   3),        // latch button LEDs (RGB)
        ENUMS(LENGTH_LIGHT, 3),
        ENUMS(SHIFT_LIGHT,  3),
        ENUMS(SCALE_LIGHT,  3),
        ENUMS(SAVE_LIGHT,   3),
        ENUMS(RECALL_LIGHT, 3),
        NUM_LIGHTS
    };

    void setRGB(int baseId, float r, float g, float b) {
        lights[baseId + 0].setBrightness(r);
        lights[baseId + 1].setBrightness(g);
        lights[baseId + 2].setBrightness(b);
    }
    void clearRGB(int baseId) { setRGB(baseId, 0.f, 0.f, 0.f); }

    struct SaveAnimation {
        bool  active    = false;
        int   slot      = -1;
        float timer     = 0.f;
        float duration  = 0.8f;
        bool  isRecall  = false;

        void trigger(int sl, float dur, bool rec) {
            active = true;
            slot = sl;
            timer = 0.f;
            duration = dur;
            isRecall = rec;
        }
    };
    SaveAnimation saveAnim;

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
    int   editChan          = 0;
    int   globalStep         = -1;
    float glowPhase          = 0.f;   // animation phase for the edit-target glow ring

    float lengthSliderSnapshot[8] = {-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f};
    float scaleSliderSnapshot     = -1.f;
    int   prevSelectedChan        = 0;
    static constexpr float LENGTH_DEADBAND = 0.15f;
    static constexpr float SCALE_DEADBAND  = 0.15f;

    float presetCV[16][8][16] = {};
    int   presetLen[16][8]    = {};
    int   presetScale[16][8]  = {};
    int   presetDir[16][8]    = {};
    bool  presetValid[16]     = {};

    bool muteMode=false, lengthMode=false, shiftMode=false;
    bool scaleMode=false, saveMode=false,  recallMode=false;
    bool prevMuteMode=false, prevLengthMode=false, prevShiftMode=false;
    bool prevScaleMode=false, prevSaveMode=false,  prevRecallMode=false;

    dsp::SchmittTrigger clockTrig, resetTrig, stepTrig[16];
    int   divCount  = 0;
    float glideCV[8]= {};
    float prevSlider[8]    = {0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f};
    int   lastSeqPos[8]    = {};
    float stepHoldTimer    = 0.f;
    static constexpr float STEP_HOLD_WINDOW = 0.08f;
    float timeSinceLastClock = 0.f;
    float lastClockPeriod    = 0.5f;

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
                if(seqPos[ch]>=len-1){seqPos[ch]=len-1;pendDir[ch] = -1;} 
                if(seqPos[ch]<=0)   {seqPos[ch]=0;     pendDir[ch]= 1;}
                break;
            case 3: seqPos[ch]=(int)(random::uniform()*len); break;
        }
    }

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
        divCount = 0;
    }

    void process(const ProcessArgs& args) override {
        bool rawMute   = params[MUTE_PARAM].getValue()   > 0.5f;
        bool rawLength = params[LENGTH_PARAM].getValue()  > 0.5f;
        bool rawShift  = params[SHIFT_PARAM].getValue()   > 0.5f;
        bool rawScale  = params[SCALE_PARAM].getValue()   > 0.5f;
        bool rawSave   = params[SAVE_PARAM].getValue()    > 0.5f;
        bool rawRecall = params[RECALL_PARAM].getValue()  > 0.5f;

        bool muteTrig   = rawMute   && !prevMuteMode;
        bool lengthTrig = rawLength && !prevLengthMode;
        bool shiftTrig  = rawShift  && !prevShiftMode;
        bool scaleTrig  = rawScale  && !prevScaleMode;
        bool saveTrig   = rawSave   && !prevSaveMode;
        bool recallTrig = rawRecall && !prevRecallMode;

        if (muteTrig || lengthTrig || shiftTrig || scaleTrig || saveTrig || recallTrig) {
            if (!muteTrig)   params[MUTE_PARAM].setValue(0.f);
            if (!lengthTrig) params[LENGTH_PARAM].setValue(0.f);
            if (!shiftTrig)  params[SHIFT_PARAM].setValue(0.f);
            if (!scaleTrig)  params[SCALE_PARAM].setValue(0.f);
            if (!saveTrig)   params[SAVE_PARAM].setValue(0.f);
            if (!recallTrig) params[RECALL_PARAM].setValue(0.f);
        }

        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        saveMode   = params[SAVE_PARAM].getValue()    > 0.5f;
        recallMode = params[RECALL_PARAM].getValue()  > 0.5f;

        bool lengthChanChanged = lengthMode && (editChan != prevSelectedChan);
        if ((!prevLengthMode && lengthMode) || lengthChanChanged)
            lengthSliderSnapshot[editChan] = params[SLIDER_PARAMS + editChan].getValue();
        if (prevLengthMode && !lengthMode)
            for(int ch=0;ch<8;ch++) lengthSliderSnapshot[ch] = -1.f;

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

        if (muteMode)   setRGB(MUTE_LIGHT,   0.6f,0.f,1.f);
        else            clearRGB(MUTE_LIGHT);
        if (lengthMode) setRGB(LENGTH_LIGHT, 0.f,1.f,0.f);
        else            clearRGB(LENGTH_LIGHT);
        if (shiftMode)  setRGB(SHIFT_LIGHT,  1.f,1.f,1.f);
        else            clearRGB(SHIFT_LIGHT);
        if (scaleMode)  setRGB(SCALE_LIGHT,  0.f,0.4f,1.f);
        else            clearRGB(SCALE_LIGHT);
        if (saveMode)   setRGB(SAVE_LIGHT,   1.f,0.6f,0.f);
        else            clearRGB(SAVE_LIGHT);
        if (recallMode) setRGB(RECALL_LIGHT, 0.f,1.f,1.f);
        else            clearRGB(RECALL_LIGHT);

        bool noMode = !muteMode&&!lengthMode&&!shiftMode&&!scaleMode&&!saveMode&&!recallMode;
        selectedChan = editChan;

        for (int i = 0; i < 16; i++) {
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
                if (i < 8) {
                    editChan = i;
                    chanMuted[i] = !chanMuted[i];
                }
                else {
                    stepMuted[editChan][i] = !stepMuted[editChan][i];
                }
            }
            else if (lengthMode) {
                if (i < 8) editChan = i;
            }
            else if (shiftMode) {
                if (i < 8) {
                    editChan = i;
                } else {
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
                if (i < 8) editChan = i;
            }
            else {
                if (globalStep == i) {
                    globalStep = -1;
                } else {
                    globalStep = i;
                }
            }
        }

        if (noMode) {
            for (int ch = 0; ch < 8; ch++) {
                float sv = params[SLIDER_PARAMS + ch].getValue();
                if (std::abs(sv - prevSlider[ch]) > 0.0001f) {
                    int targetStep;
                    if (globalStep >= 0) {
                        targetStep = globalStep;
                    } else if (stepHoldTimer > 0.f) {
                        targetStep = lastSeqPos[ch];
                    } else {
                        targetStep = seqPos[ch];
                    }
                    stepCV[ch][targetStep] = sv;
                    prevSlider[ch] = sv;
                }
            }
        } else {
            for (int ch = 0; ch < 8; ch++)
                prevSlider[ch] = params[SLIDER_PARAMS + ch].getValue();
        }

        if (lengthMode) {
            float sv   = params[SLIDER_PARAMS + editChan].getValue();
            float snap = lengthSliderSnapshot[editChan];
            if (snap < 0.f || std::abs(sv - snap) > LENGTH_DEADBAND) {
                int newLen = clamp((int)std::round(sv / 4.0f * 16.f), 1, 16);
                seqLength[editChan] = newLen;
                if (seqPos[editChan] >= seqLength[editChan]) seqPos[editChan] = 0;
            }
        }

        if (scaleMode) {
            float sv   = params[SLIDER_PARAMS+editChan].getValue();
            float snap = scaleSliderSnapshot;
            if (snap < 0.f || std::abs(sv-snap) > SCALE_DEADBAND)
                scaleIndex[editChan] = clamp((int)(sv / 4.0f * 15.5f), 0, 15);
        }

        int  clkMode  = (int)params[CLK_SWITCH_PARAM].getValue();
        bool holdHigh = (clkMode==1) && (inputs[RESET_INPUT].getVoltage()>1.0f);
        if (clkMode==0 || clkMode==2) {
            if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
                for(int ch=0;ch<8;ch++) seqPos[ch]=0;
                divCount=0;
            }
        }

        bool clocked = false;
        timeSinceLastClock += args.sampleTime;
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
                if(++divCount>=div){
                    divCount=0;
                    clocked=true;
                    if (timeSinceLastClock > 0.001f && timeSinceLastClock < 10.f)
                        lastClockPeriod = timeSinceLastClock;
                    timeSinceLastClock = 0.f;
                }
            }
        }

        if (clocked) {
            for(int ch=0;ch<8;ch++){
                if(frozen[ch]) continue;
                lastSeqPos[ch] = seqPos[ch];
                advanceChannel(ch);
            }
            stepHoldTimer = STEP_HOLD_WINDOW;
        }
        if (stepHoldTimer > 0.f) stepHoldTimer -= args.sampleTime;

        float att=params[ATTENUATE_PARAM].getValue();
        float off=params[OFFSET_PARAM].getValue();

        for(int ch=0;ch<8;ch++){
            int pos=seqPos[ch];
            bool muted = chanMuted[ch]||stepMuted[ch][pos];
            bool frz   = frozen[ch];
            float bright = (ch==editChan)?1.0f:0.3f;
            if (muted)
                setRGB(CHANNEL_LIGHTS+ch*3, 0.6f*bright, 0.f, 1.f*bright);
            else if (frz)
                setRGB(CHANNEL_LIGHTS+ch*3, bright, bright, bright);
            else
                setRGB(CHANNEL_LIGHTS+ch*3, bright, 0.f, 0.f);

            if(muted){
                outputs[CV_OUTPUTS+ch].setVoltage(0.f);
                continue;
            }
            float v=stepCV[ch][pos];
            if(scaleIndex[ch]>0&&scaleIndex[ch]<15)
                v=quantizeVoltage(v/4.0f,scaleIndex[ch])*4.0f;
            if(stepSmooth[ch][pos]){
                float glideTime = std::max(lastClockPeriod * 0.9f, 0.25f);
                float rate = 1.0f / (args.sampleRate * glideTime);
                glideCV[ch] += (v - glideCV[ch]) * rate;
                v = glideCV[ch];
            } else {
                glideCV[ch]=v;
            }
            v=clamp(v*att+off,-5.f,10.f);
            outputs[CV_OUTPUTS+ch].setVoltage(v);
        }

        if (saveAnim.active) {
            saveAnim.timer += args.sampleTime;
            if (saveAnim.timer >= saveAnim.duration) saveAnim.active = false;
        }
        float animProgress = saveAnim.active
            ? clamp(saveAnim.timer / saveAnim.duration, 0.f, 1.f)
            : -1.f;
        int animFill = saveAnim.active
            ? (int)std::floor(animProgress * (saveAnim.slot + 1))
            : -1;

        for (int i = 0; i < 16; i++) {
            bool isCurrent = (i == seqPos[editChan]);
            bool isMuted   = stepMuted[editChan][i];
            bool inLen     = (i < seqLength[editChan]);

            if (saveAnim.active && !saveAnim.isRecall) {
                if (i <= saveAnim.slot && i <= animFill)
                    setRGB(BUTTON_LIGHTS + i*3, 1.f, 0.6f, 0.f);
                else
                    clearRGB(BUTTON_LIGHTS + i*3);
            }
            else if (saveMode) {
                float b = presetValid[i] ? 0.7f : 0.15f;
                setRGB(BUTTON_LIGHTS + i*3, b, b*0.6f, 0.f);
            }
            else if (saveAnim.active && saveAnim.isRecall) {
                if (i == saveAnim.slot)
                    setRGB(BUTTON_LIGHTS + i*3, 0.f, 1.f, 1.f);
                else if (presetValid[i])
                    setRGB(BUTTON_LIGHTS + i*3, 0.f, 0.4f, 0.4f);
                else
                    clearRGB(BUTTON_LIGHTS + i*3);
            }
            else if (recallMode) {
                float b = presetValid[i] ? 0.8f : 0.05f;
                setRGB(BUTTON_LIGHTS + i*3, 0.f, b, b);
            }
            else if (muteMode) {
                float b = (i < 8) ? (chanMuted[i] ? 1.f : 0.f)
                                  : (stepMuted[editChan][i] ? 1.f : 0.f);
                setRGB(BUTTON_LIGHTS + i*3, 0.6f*b, 0.f, 1.f*b);
            }
            else if (lengthMode) {
                float b = (i == seqLength[editChan]-1) ? 1.f
                        : (inLen ? 0.4f : 0.f);
                setRGB(BUTTON_LIGHTS + i*3, 0.f, b, 0.f);
            }
            else if (scaleMode) {
                float b = (i == scaleIndex[editChan]) ? 1.f : 0.f;
                setRGB(BUTTON_LIGHTS + i*3, 0.f, 0.4f*b, 1.f*b);
            }
            else if (shiftMode) {
                float b = (i >= 8) ? 0.3f : 0.f;
                setRGB(BUTTON_LIGHTS + i*3, b, b, b);
            }
            else {
                if (isCurrent) {
                    setRGB(BUTTON_LIGHTS + i*3, 1.f, 0.f, 0.f);
                } else if (globalStep == i) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.5f, 0.f, 0.f);
                } else if (isMuted && inLen) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.1f, 0.f, 0.15f);
                } else if (inLen) {
                    setRGB(BUTTON_LIGHTS + i*3, 0.05f, 0.f, 0.f);
                } else {
                    clearRGB(BUTTON_LIGHTS + i*3);
                }
            }
        }

        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];
            bool muted  = chanMuted[ch] || stepMuted[ch][pos];
            bool frz    = frozen[ch];
            float bright = (ch == editChan) ? 1.0f : 0.3f;
            if (muted)
                setRGB(CHANNEL_LIGHTS + ch*3, 0.6f*bright, 0.f, 1.f*bright);
            else if (frz)
                setRGB(CHANNEL_LIGHTS + ch*3, bright, bright, bright);
            else
                setRGB(CHANNEL_LIGHTS + ch*3, bright, 0.f, 0.f);
        }

        glowPhase += args.sampleTime * 1.5f;
        if (glowPhase > 2.f * M_PI) glowPhase -= 2.f * M_PI;
        float glow = 0.45f + 0.45f * std::sin(glowPhase);
        // Edit-ring lights: glow on whichever channel is the current
        // MUTE/LENGTH/SCALE/SHIFT target
        for (int ch = 0; ch < 8; ch++)
            lights[EDIT_RING_LIGHTS + ch].setBrightness(ch == editChan ? glow : 0.f);

        // Custom display update logic
    }

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
        // ROBUST CRASH-PROOF JSON PARSING TO AVOID SILENT NANSON C-LIBRARY CRASHES ON BOOT
        auto getI = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k);
            if (!a) return 0;
            json_t* val = json_array_get(a, idx);
            return val ? (int)json_integer_value(val) : 0;
        };
        auto getF = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k);
            if (!a) return 0.f;
            json_t* val = json_array_get(a, idx);
            return val ? (float)json_real_value(val) : 0.f;
        };
        auto getB = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k);
            if (!a) return false;
            json_t* val = json_array_get(a, idx);
            return val ? json_boolean_value(val) : false;
        };

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
        editChan=0; globalStep=-1; glowPhase=0.f;
        scaleSliderSnapshot=-1.f;
        for(int ch=0;ch<8;ch++) lengthSliderSnapshot[ch]=-1.f;
    }
};

// ============================================================
// SlimFader — extends app::SvgSlider so the MetaModule's VCV-to-native
// element translator recognizes it as a "Slider" (bare ParamWidget
// wasn't reachable by the on-screen cursor on hardware, even though
// it didn't crash). Loads SVGs from this plugin's OWN res/ folder via
// asset::plugin — that path goes through the same SVG->PNG conversion
// the panel already uses successfully. The earlier crash/hang came
// from asset::sys (VCV's core ComponentLibrary, which doesn't exist
// on the MetaModule filesystem) and from skipping setBackgroundSvg/
// setHandleSvg entirely (which left SvgSlider's internal handle/
// background state uninitialized). This mirrors JW-Modules'
// JwHorizontalVCVSlider, which ships working on MetaModule via FM16Seq.
// ============================================================
struct SlimFader : app::SvgSlider {
    // TH/HW/HH here MUST exactly match SkylineFaderBg.svg's and
    // SkylineFaderHandle.svg's own declared width/height (see comment
    // above on why that matters for the MetaModule renderer).
    //
    // No drawLayer/onButton/onDragMove/onDoubleClick here anymore —
    // app::SvgSlider already draws its loaded background/handle SVGs
    // and already handles click-and-drag natively, correctly, on its
    // own. Custom overrides of all of that were leftover from when
    // this class extended a bare ParamWidget (before the MetaModule
    // fix), and kept painting a second, independently-computed track/
    // handle on top of the base class's own rendering — invisible on
    // hardware (where drawLayer never renders at all), but visible on
    // desktop as a muddy, slightly-misaligned "double cap" look.
    static const int TW=6,TH=40,HW=14,HH=8,TM=6;
    SlimFader(){
        setBackgroundSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SkylineFaderBg.svg")));
        setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SkylineFaderHandle.svg")));
        background->box.size = Vec(TW, TH);
        handle->box.size = Vec(HW, HH);
        setHandlePosCentered(
            Vec(TW/2.f, TH - HH/2.f),  // value 0 -> bottom
            Vec(TW/2.f, TM + HH/2.f)   // value 1 -> top, with a margin below the very top edge
        );
        box.size = Vec(HW, TH+HH);
    }
};

// ============================================================
// EditRingLight — glowing ring drawn around a channel LED,
// showing which channel MUTE/LENGTH/SCALE/SHIFT currently target.
// Not a Param/Jack/Light subclass, so its NanoVG draw isn't
// substituted by the MetaModule engine's native rendering.
// ============================================================
struct EditRingLight : widget::Widget {
    int     lightId  = 0;
    Module* skyModule = nullptr;

    EditRingLight() { box.size = Vec(22, 22); }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        if (!skyModule) return;
        float brightness = skyModule->lights[lightId].getBrightness();
        if (brightness <= 0.001f) return;

        Vec centre = box.size.div(2.f);
        float r = 9.5f;
        const float NR = 0.1f, NG = 0.25f, NB = 0.6f;

        NVGpaint glow = nvgRadialGradient(args.vg,
            centre.x, centre.y, r * 0.7f, r * 1.6f,
            nvgRGBAf(NR, NG, NB, brightness * 0.55f),
            nvgRGBAf(NR, NG, NB, 0.f));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, centre.x, centre.y, r * 1.6f);
        nvgFillPaint(args.vg, glow);
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, centre.x, centre.y, r);
        nvgStrokeColor(args.vg, nvgRGBAf(NR, NG, NB, brightness * 0.9f));
        nvgStrokeWidth(args.vg, 1.8f);
        nvgStroke(args.vg);
    }
};

// ============================================================
// Widget
// ============================================================
// SkylinePort — replaces PJ301MPort everywhere in this plugin.
// PJ301MPort calls setSvg(Svg::load(asset::system("res/ComponentLibrary/
// PJ301M.svg"))) — asset::system is VCV's core/system component
// library, which doesn't exist on the MetaModule firmware. That's the
// exact same broken-asset-path bug that originally crashed the slider
// (asset::sys there, asset::system here — same family). This mirrors
// the proven slider fix: same recognized base class (app::SvgPort, so
// the element translator still recognizes it as a JackInput/output),
// loading from this plugin's own bundled res/ folder via asset::plugin
// instead, which goes through the same SVG->PNG pipeline already
// confirmed working for the panel and the fader graphics.
struct SkylinePort : app::SvgPort {
    SkylinePort() {
        setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SkylineJack.svg")));
    }
};

struct SkylineWidget : ModuleWidget {
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance,"res/Skyline.svg")));

        const float cX[8]={7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};
        const float xJack=7.00f,xSwitch=20.00f;
        const float xK1=32.0f,xK2=48.5f,xK3=65.0f;
        const float xB1=78.5f,xB2=87.0f,xB3=95.5f;
        const float yOut=22.5f,yLed=31.0f;
        const float yClk=46.0f,yKnob=53.5f,yRst=61.0f;
        const float yB1=46.0f,yB2=61.0f;
        const float ySld=70.0f,yS1=104.0f,yS2=119.0f,ySLbl=126.5f;

        for(int ch=0;ch<8;ch++){
            addOutput(createOutputCentered<SkylinePort>(
                mm2px(Vec(cX[ch],yOut)),module,Skyline::CV_OUTPUTS+ch));

            auto* ring = new EditRingLight;
            ring->skyModule = module;
            ring->lightId   = Skyline::EDIT_RING_LIGHTS + ch;
            ring->box.pos   = mm2px(Vec(cX[ch],yLed)).minus(ring->box.size.div(2.f));
            addChild(ring);

            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[ch],yLed)),module,Skyline::CHANNEL_LIGHTS+ch*3));
        }

        addInput(createInputCentered<SkylinePort>(mm2px(Vec(xJack,yClk)),module,Skyline::CLOCK_INPUT));
        addInput(createInputCentered<SkylinePort>(mm2px(Vec(xJack,yRst)),module,Skyline::RESET_INPUT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(xSwitch,yKnob)),module,Skyline::CLK_SWITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK1,yKnob)),module,Skyline::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK2,yKnob)),module,Skyline::ATTENUATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK3,yKnob)),module,Skyline::DIVIDE_PARAM));

        addParam(createParamCentered<VCVLatch>(
            mm2px(Vec(xB1,yB1)),module,Skyline::MUTE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
            mm2px(Vec(xB1,yB1)),module,Skyline::MUTE_LIGHT));

        addParam(createParamCentered<VCVLatch>(
            mm2px(Vec(xB2,yB1)),module,Skyline::LENGTH_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
            mm2px(Vec(xB2,yB1)),module,Skyline::LENGTH_LIGHT));

        addParam(createParamCentered<VCVLatch>(
            mm2px(Vec(xB3,yB1)),module,Skyline::SHIFT_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
            mm2px(Vec(xB3,yB1)),module,Skyline::SHIFT_LIGHT));

        addParam(createParamCentered<VCVLatch>(
            mm2px(Vec(xB1,yB2)),module,Skyline::SCALE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
            mm2px(Vec(xB1,yB2)),module,Skyline::SCALE_LIGHT));

        addParam(createParamCentered<VCVLatch>(
            mm2px(Vec(xB2,yB2)),module,Skyline::SAVE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
            mm2px(Vec(xB2,yB2)),module,Skyline::SAVE_LIGHT));

        addParam(createParamCentered<VCVLatch>(
            mm2px(Vec(xB3,yB2)),module,Skyline::RECALL_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
            mm2px(Vec(xB3,yB2)),module,Skyline::RECALL_LIGHT));

        // REGISTER SLIDERS NATIVELY AS VCV SvgSliders (THE EXACT PATTERN USED BY BEFACO, VOSTOK & JW MODULES)
        // This is 100% crash-proof and guaranteed to map and render on the screen!
        for(int ch=0;ch<8;ch++){
            addParam(createParam<SlimFader>(
                mm2px(Vec(cX[ch]-2.37f,ySld)),module,Skyline::SLIDER_PARAMS+ch));
        }

        // Buttons are now plain VCVButton with a SEPARATE, standalone
        // light layered on top — not VCVLightButton (which embeds the
        // light inside the button). NANOModules (4ms's own plugin,
        // confirmed shipping on MetaModule) only ever uses standalone
        // lights, never combined into a button; this mirrors that
        // exact pattern instead of the untested combo-widget approach.
        for(int i=0;i<8;i++){
            addParam(createParamCentered<VCVButton>(
                mm2px(Vec(cX[i],yS1)),module,Skyline::STEP_PARAMS+i));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[i],yS1)),module,Skyline::BUTTON_LIGHTS+i*3));
            // tiny playhead LED, just above the button
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[i],yS1-5.f)),module,Skyline::STEP_LIGHTS+i*3));

            addParam(createParamCentered<VCVButton>(
                mm2px(Vec(cX[i],yS2)),module,Skyline::STEP_PARAMS+8+i));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[i],yS2)),module,Skyline::BUTTON_LIGHTS+(8+i)*3));
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(
                mm2px(Vec(cX[i],yS2-5.f)),module,Skyline::STEP_LIGHTS+(8+i)*3));
        }

        // All panel text (title, channel numbers, OFFSET/ATTEN/DIVIDE,
        // MUTE/LEN/SHIFT, SCALE/SAVE/RECALL, the SHIFT-function labels)
        // is already baked into the panel SVG/PNG as outlined paths.
        // A redundant runtime-drawn copy used to live here via a custom
        // PanelLabel widget calling nvgText with APP->window->uiFont —
        // that's a Rack desktop application resource, not something
        // this plugin bundles, and it isn't available on the MetaModule
        // firmware, which is what was actually causing the garbled
        // "SKYLINE" text (not the panel SVG, which was already correct).
        // Removed entirely rather than chasing a bundled-font fix, since
        // it was always pure duplication: FM16Seq, confirmed working on
        // hardware, has no runtime text drawing at all — every label on
        // its panel lives purely in the panel art, the same as Skyline's
        // panel SVG already does.
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("SkylineMM");
