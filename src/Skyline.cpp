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
    // Japanese (In scale)
    {0,1,5,7,10,0,1,5,7,10,0,1},
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
        CLK_SWITCH_PARAM,  // 0=CLK 1=CV 2=SLAVE
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
    dsp::SchmittTrigger saveTrig;
    dsp::SchmittTrigger recallTrig;
    dsp::SchmittTrigger muteTrig;
    dsp::SchmittTrigger lengthTrig;
    dsp::SchmittTrigger shiftTrig;
    dsp::SchmittTrigger scaleTrig;
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
        configSwitch(CLK_SWITCH_PARAM, 0.f, 2.f, 0.f, "Clock Mode", {"CLK", "CV", "SLAVE"});

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
        // VCVLightLatch params - read directly for toggle modes
        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        // SAVE/RECALL are momentary VCVButton - use edge detection
        if (saveTrig.process(params[SAVE_PARAM].getValue()))
            saveMode = !saveMode;
        if (recallTrig.process(params[RECALL_PARAM].getValue()))
            recallMode = !recallMode;

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
                    // Steps 1-8 select channel; any step sets length
                    if (i < 8) selectedChan = i;
                    seqLength[selectedChan] = i + 1;
                    if (seqPos[selectedChan] >= seqLength[selectedChan])
                        seqPos[selectedChan] = 0;
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
        // CLK/CV/SLAVE switch
        int clkMode = (int)params[CLK_SWITCH_PARAM].getValue();
        
        bool holdHigh = inputs[RESET_INPUT].getVoltage() > 1.0f;
        if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
            for (int ch = 0; ch < 8; ch++) seqPos[ch] = 0;
            divCount = 0;
        }

        // ---- Clock ----
        bool clocked = false;
        
        if (clkMode == 1) {
            // CV mode: use clock input voltage to select step position
            float cvVoltage = inputs[CLOCK_INPUT].getVoltage();
            int newPos = (int)(clamp(cvVoltage / 10.f, 0.f, 1.f) * 15.f);
            for (int ch = 0; ch < 8; ch++) seqPos[ch] = newPos;
        }
        else if (!holdHigh && clkMode == 0 && clockTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
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
                // Live recording disabled - use step-hold to record values
                // (enabling live recording overwrites all steps with same value)
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
        // VCVLightLatch drives its own LED - no manual update needed
        // Channel lights show which channel is selected
        for (int ch = 0; ch < 8; ch++)
            lights[CHANNEL_LIGHTS + ch].setBrightness(ch == selectedChan ? 1.f : 0.15f);
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

        json_t* pdirs = json_array();
        for (int ch = 0; ch < 8; ch++)
            json_array_append_new(pdirs, json_integer(pendDir[ch]));
        json_object_set_new(root, "pendDir", pdirs);

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
        j = json_object_get(root, "stepMuted");
        if (j) { int idx=0; for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) stepMuted[ch][s]=json_boolean_value(json_array_get(j,idx++)); }
        j = json_object_get(root, "frozen");
        if (j) for(int ch=0;ch<8;ch++) frozen[ch]=json_boolean_value(json_array_get(j,ch));
        j = json_object_get(root, "pendDir");
        if (j) for(int ch=0;ch<8;ch++) pendDir[ch]=(int)json_integer_value(json_array_get(j,ch));
    }  // end dataFromJson
    void onRandomize(const RandomizeEvent& e) override {
        // Only randomize CV step values - not mode/button states
        for (int ch = 0; ch < 8; ch++)
            for (int s = 0; s < 16; s++)
                stepCV[ch][s] = random::uniform() * 5.f;
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        // Clear all step data
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++) {
                stepCV[ch][s]    = 0.f;
                stepMuted[ch][s] = false;
                stepSmooth[ch][s]= false;
            }
            seqLength[ch]  = 16;
            seqPos[ch]     = 0;
            direction[ch]  = 0;
            scaleIndex[ch] = 0;
            chanMuted[ch]  = false;
            frozen[ch]     = false;
            glideCV[ch]    = 0.f;
        }
        selectedChan = 0;
        divCount     = 0;
    }

};  // end Skyline struct


// ============================================================
// Custom slim vertical fader - click/drag up=max down=min
// ============================================================
struct SlimFader : app::ParamWidget {
    float trackW  = 6.f;
    float trackH  = 60.f;
    float handleW = 14.f;
    float handleH = 8.f;
    bool  dragging = false;
    float dragY    = 0.f;
    float dragVal  = 0.f;

    SlimFader() {
        box.size = Vec(handleW, trackH + handleH);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float val = getParamQuantity() ? getParamQuantity()->getScaledValue() : 0.f;
        float handleY = (1.f - val) * trackH;

        // Track background
        float tx = (box.size.x - trackW) / 2.f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, tx, handleH/2.f, trackW, trackH, 3.f);
        nvgFillColor(args.vg, nvgRGB(0xc0, 0xbd, 0xb6));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(0xaa, 0xaa, 0xaa));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // Track fill (below handle)
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, tx, handleH/2.f + handleY, trackW, trackH - handleY, 3.f);
        nvgFillColor(args.vg, nvgRGB(0x99, 0x20, 0x20));
        nvgFill(args.vg);

        // Handle
        float hx = (box.size.x - handleW) / 2.f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, hx, handleY, handleW, handleH, 2.f);
        nvgFillColor(args.vg, nvgRGB(0x33, 0x33, 0x33));
        nvgFill(args.vg);
        // Handle centre line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, hx + 2.f, handleY + handleH/2.f);
        nvgLineTo(args.vg, hx + handleW - 2.f, handleY + handleH/2.f);
        nvgStrokeColor(args.vg, nvgRGB(0x88, 0x88, 0x88));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
    }

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging = true;
            dragY    = e.pos.y;
            dragVal  = getParamQuantity() ? getParamQuantity()->getScaledValue() : 0.f;
            e.consume(this);
        }
        if (e.action == GLFW_RELEASE) {
            dragging = false;
        }
        ParamWidget::onButton(e);
    }

    void onDragMove(const DragMoveEvent& e) override {
        if (!dragging || !getParamQuantity()) return;
        float delta = -e.mouseDelta.y / trackH;
        float newVal = clamp(dragVal + delta, 0.f, 1.f);
        dragVal = newVal;
        getParamQuantity()->setScaledValue(newVal);
    }

    void onDoubleClick(const DoubleClickEvent& e) override {
        if (getParamQuantity()) getParamQuantity()->reset();
    }
};

struct SkylineWidget : ModuleWidget {
    SkylineWidget(Skyline* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Skyline.svg")));

        // Screws at corners
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2*RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2*RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // SVG px->mm: scale = 101.6/300 = 0.3387 mm/px
        // Channel X (mm): 7.00, 19.51, 32.03, 44.54, 57.06, 69.57, 82.09, 94.60
        const float cX[8] = {7.00f, 19.51f, 32.03f, 44.54f, 57.06f, 69.57f, 82.09f, 94.60f};

        // 8 CV outputs  y=22.5mm  |  Channel LEDs  y=31.0mm
        for (int ch = 0; ch < 8; ch++) {
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(cX[ch], 22.5f)), module, Skyline::CV_OUTPUTS + ch));
            addChild(createLightCentered<SmallLight<RedLight>>(
                mm2px(Vec(cX[ch], 31.0f)), module, Skyline::CHANNEL_LIGHTS + ch));
        }

        // CLK/CV input  y=46.5mm  |  RST/HLD input  y=59.5mm
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.00f, 46.5f)), module, Skyline::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.00f, 59.5f)), module, Skyline::RESET_INPUT));

        // CLK/CV/SLAVE switch  x=16.5mm  y=51.0mm (centred between jacks)
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(16.5f, 51.0f)), module, Skyline::CLK_SWITCH_PARAM));

        // OFFSET / ATTEN / DIVIDE knobs  y=53.5mm
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.0f, 53.5f)), module, Skyline::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(47.5f, 53.5f)), module, Skyline::ATTENUATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(66.0f, 53.5f)), module, Skyline::DIVIDE_PARAM));

        // Mode buttons row 1  y=46.5mm  (MUTE, LENGTH, SHIFT)
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(
            mm2px(Vec(79.5f, 46.5f)), module, Skyline::MUTE_PARAM,   Skyline::MUTE_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(87.5f, 46.5f)), module, Skyline::LENGTH_PARAM, Skyline::LENGTH_LIGHT));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(95.5f, 46.5f)), module, Skyline::SHIFT_PARAM,  Skyline::SHIFT_LIGHT));

        // Mode buttons row 2  y=59.5mm  (SCALE, SAVE, RECALL)
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
            mm2px(Vec(79.5f, 59.5f)), module, Skyline::SCALE_PARAM,  Skyline::SCALE_LIGHT));
        addParam(createParamCentered<VCVButton>(
            mm2px(Vec(87.5f, 59.5f)), module, Skyline::SAVE_PARAM));
        addParam(createParamCentered<VCVButton>(
            mm2px(Vec(95.5f, 59.5f)), module, Skyline::RECALL_PARAM));

        // 8 slim vertical faders  top at y=69.5mm
        for (int ch = 0; ch < 8; ch++) {
            auto* fader = createParam<SlimFader>(
                mm2px(Vec(cX[ch] - 1.36f, 69.5f)), module, Skyline::SLIDER_PARAMS + ch);
            addParam(fader);
        }

        // Step buttons row 1  y=105.5mm  |  row 2  y=120.5mm
        for (int i = 0; i < 8; i++) {
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(cX[i], 105.5f)), module,
                Skyline::STEP_PARAMS + i,   Skyline::STEP_LIGHTS + i));
            addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(
                mm2px(Vec(cX[i], 120.5f)), module,
                Skyline::STEP_PARAMS + 8+i, Skyline::STEP_LIGHTS + 8+i));
        }

        // ---- Panel labels (NanoVG, drawn over SVG) ----
        struct PanelLabel : widget::Widget {
            std::string text;
            float fontSize;
            NVGcolor color;
            PanelLabel(Vec centre, std::string t, float sz, NVGcolor c) {
                box.pos = centre.minus(Vec(40, 8));
                box.size = Vec(80, 16);
                text = t; fontSize = sz; color = c;
            }
            void draw(const DrawArgs& args) override {
                nvgFontSize(args.vg, fontSize);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, color);
                nvgText(args.vg, box.size.x/2, box.size.y/2, text.c_str(), NULL);
            }
        };
        auto addPL = [&](float x_mm, float y_mm, std::string t, float sz = 10.f,
                         NVGcolor c = nvgRGB(0x44,0x44,0x44)) {
            addChild(new PanelLabel(mm2px(Vec(x_mm, y_mm)), t, sz, c));
        };

        // Title
        addPL(50.8f,  4.5f, "SKYLINE",              12.f, nvgRGB(0x11,0x11,0x11));
        addPL(50.8f,  9.5f, "8 CHANNEL CV SEQUENCER", 7.f, nvgRGB(0x88,0x88,0x88));

        // Channel numbers  y=13.5mm
        for (int i = 0; i < 8; i++)
            addPL(cX[i], 13.5f, std::to_string(i+1), 9.f);

        // Left column labels
        addPL(7.00f, 40.5f, "CLK/CV",  7.5f);
        addPL(7.00f, 55.5f, "RST/HLD", 7.5f);

        // Knob labels  y=40.5mm
        addPL(29.0f, 40.5f, "OFFSET", 7.5f);
        addPL(47.5f, 40.5f, "ATTEN",  7.5f);
        addPL(66.0f, 40.5f, "DIVIDE", 7.5f);

        // Mode button labels — row1 above y=40.5mm, row2 between y=54.5mm
        addPL(79.5f, 40.5f, "MUTE",   7.5f);
        addPL(87.5f, 40.5f, "LEN",    7.5f);
        addPL(95.5f, 40.5f, "SHIFT",  7.5f);
        addPL(79.5f, 54.5f, "SCALE",  7.5f);
        addPL(87.5f, 54.5f, "SAVE",   7.5f);
        addPL(95.5f, 54.5f, "RECALL", 7.5f);

        // Step function labels  y=127.0mm
        addPL(cX[0], 127.0f, "CLEAR",   7.f);
        addPL(cX[1], 127.0f, "SMOOTH",  7.f);
        addPL(cX[2], 127.0f, "RND",     7.f);
        addPL(cX[3], 127.0f, "FREEZE",  7.f);
        addPL(cX[4], 127.0f, "FWD",     7.f);
        addPL(cX[5], 127.0f, "REV",     7.f);
        addPL(cX[6], 127.0f, "PEND",    7.f);
        addPL(cX[7], 127.0f, "RND SEQ", 7.f);
    }
};


Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");