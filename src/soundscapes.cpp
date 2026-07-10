#include "soundscapes.hpp"

/**
 * Soundscapes Module Constructor
 */
Soundscapes::Soundscapes() {
    // 1. Initialise the VCV Rack module engine limits
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

    // 2. Configure Sidebar Parameter Inputs
    configInput(CLK_INPUT, "Clock CV Trigger");
    configInput(RST_INPUT, "Reset CV Trigger");
    configInput(VOCT_INPUT, "1V/Oct Pitch Modulation");
    configInput(GATE_INPUT, "Envelope Gate Trigger");
    configInput(VEL_INPUT, "Velocity Control CV");
    configInput(EXT_INPUT, "External Audio Carrier Input");
    configInput(DUCK_INPUT, "Sidechain Duck Envelope Follower CV");

    // 3. Configure Row 1 Channel Output Sockets
    for (int i = 0; i < 8; i++) {
        configOutput(CH1_OUTPUT + i, string::f("Channel %d Audio Output", i + 1));
    }

    // 4. Configure Row 2 Core Synth Parameters
    // Corrected: Sets maximum MODE_PARAM value limit to 2.0f (allows 3 positions: 0, 1, 2)
    configParam(MODE_PARAM, 0.0f, 2.0f, 0.0f, "Synthesis Mode Select (Voices -> Waves -> Drone & Dust)");
    configParam(FM_PARAM, 0.0f, 1.0f, 0.0f, "FM Modulation Send Edit Select");
    configParam(DELAY_PARAM, 0.0f, 1.0f, 0.0f, "Delay Send Edit Select");
    configParam(REVERB_PARAM, 0.0f, 1.0f, 0.0f, "Reverb Send Edit Select");
    configParam(FILTER_PARAM, 0.0f, 1.0f, 0.0f, "Filter Send Edit Select");

    configParam(RATE_PARAM, 0.0f, 1.0f, 0.5f, "Macro 1: Sequencer Rate / Delay Time");
    configParam(DENSITY_PARAM, 0.0f, 1.0f, 0.5f, "Macro 2: Noise Dust Rate / Filter Resonance");
    configParam(TIMBRE_PARAM, 0.0f, 1.0f, 0.5f, "Macro 3: FM Harmonic Ratio / Reverb Shimmer Pitch");
    configParam(TEXTURE_PARAM, 0.0f, 1.0f, 0.5f, "Macro 4: FM Index Modulation / Filter Cutoff");
    configParam(SPREAD_PARAM, 0.0f, 1.0f, 0.5f, "Macro 5: Stereo Spatial Detune / Delay Feedback");
    configParam(DYNAMICS_PARAM, 0.0f, 1.0f, 0.5f, "Macro 6: LPG Low-pass Envelope / Reverb Decay");

    // 5. Configure Row 3 Mixer Faders & Center Quantizers
    for (int i = 0; i < 8; i++) {
        configParam(FADER1_PARAM + i, 0.0f, 1.0f, 0.8f, string::f("Channel %d Amplitude/Send Fader", i + 1));
    }
    configParam(ROOT_PARAM, 0.0f, 1.0f, 0.0f, "Diatonic Quantizer: Root Scale Transposition Note");
    configParam(SCALE_PARAM, 0.0f, 1.0f, 0.0f, "Diatonic Quantizer: Selected Harmonised Scale Degrees");

    // 6. Configure Row 4 Step Buttons & Performance Block
    for (int i = 0; i < 16; i++) {
        configParam(STEP_PARAM_START + i, 0.0f, 1.0f, 0.0f, string::f("Step Pad %d Toggle Trigger", i + 1));
    }

    configParam(PLAY_PARAM, 0.0f, 1.0f, 0.0f, "Transport: Sequencer Play / Stop Toggle");
    configParam(SHFT_PARAM, 0.0f, 1.0f, 0.0f, "Shift Toggle: Edits Step Probability & Resets Patterns");
    configParam(ARP_PARAM, 0.0f, 1.0f, 0.0f, "Arpeggiator Enable");
    configParam(FRZ_PARAM, 0.0f, 1.0f, 0.0f, "Freeze Capture State");
    configParam(CHRD_PARAM, 0.0f, 1.0f, 0.0f, "Chord / Sequence Initialize");
    configParam(PROB_PARAM, 0.0f, 1.0f, 0.0f, "Probability / Variation Lock");
    configParam(SAVE_PARAM, 0.0f, 1.0f, 0.0f, "Save Active Sequencer Pattern");
    configParam(RCL_PARAM, 0.0f, 1.0f, 0.0f, "Recall Saved Pattern");

    // Initialize Default Sequencer Trigger States
    initializeSequence();
}

/**
 * Main Process Thread
 */
void Soundscapes::process(const ProcessArgs& args) {
    // 1. Tick Sequencer Step clocks & handle focused fader locks
    processSequencer(args.sampleTime);
    handleFaderMapping();

    // 2. Temporary HUD Displays update timers
    for (int i = 0; i < 8; i++) {
        if (displayValueTimer[i] > 0.0f) {
            displayValueTimer[i] -= args.sampleTime;
        }
    }

    // 3. Monitor fader movements to display active percentages (00 - 99)
    for (int i = 0; i < 8; i++) {
        static float prevFaderVal[8] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        float currVal = params[FADER1_PARAM + i].getValue();
        if (prevFaderVal[i] >= 0.0f && fabs(currVal - prevFaderVal[i]) > 0.001f) {
            displayValueTimer[i] = 1.5f;
            displayValue[i] = currVal;
            displayType[i] = 0;
        }
        prevFaderVal[i] = currVal;
    }

    // 4. Monitor macro knobs to display edited levels across all screens
    for (int k = 0; k < 6; k++) {
        static float prevKnobVal[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        float currVal = params[RATE_PARAM + k].getValue();
        if (prevKnobVal[k] >= 0.0f && fabs(currVal - prevKnobVal[k]) > 0.001f) {
            for (int i = 0; i < 8; i++) {
                displayValueTimer[i] = 1.5f;
                displayValue[i] = currVal;
                displayType[i] = 0;
            }
        }
        prevKnobVal[k] = currVal;
    }

    // 5. Monitor ROOT & SCALE edits to display note names and scale abbreviations
    static float prevRoot = -1.0f;
    float currRoot = params[ROOT_PARAM].getValue();
    if (prevRoot >= 0.0f && fabs(currRoot - prevRoot) > 0.01f) {
        for (int i = 0; i < 8; i++) {
            displayValueTimer[i] = 1.5f;
            displayValue[i] = currRoot;
            displayType[i] = 1;
        }
    }
    prevRoot = currRoot;

    static float prevScale = -1.0f;
    float currScale = params[SCALE_PARAM].getValue();
    if (prevScale >= 0.0f && fabs(currScale - prevScale) > 0.01f) {
        for (int i = 0; i < 8; i++) {
            displayValueTimer[i] = 1.5f;
            displayValue[i] = currScale;
            displayType[i] = 2;
        }
    }
    prevScale = currScale;

    // Call Synthesizer Engine to process raw Voice DSP output signals
    processDSP(args);

    // Cascade FX Tank Bus Processing
    float sampleRate = args.sampleRate;
    float inputBusL = 0.0f;
    float inputBusR = 0.0f;

    // Accumulate stereo audio signals from active voice channel outputs
    for (int i = 0; i < 8; i++) {
        // Sum mono outputs as base dry signal (balanced spatially across stereo field)
        float panL = 1.0f - ((float)i / 7.0f);
        float panR = (float)i / 7.0f;

        float drySignal = outputs[CH1_OUTPUT + i].getVoltage() / 5.0f; // Scale back to internal unit scale

        inputBusL += drySignal * panL;
        inputBusR += drySignal * panR;
    }

    // --- PROCESSOR 1: FEEDBACK DELAY ---
    float delaySendVal = params[DELAY_PARAM].getValue();
    float fbackVal = params[SPREAD_PARAM].getValue() * 0.95f; // Max feedback clamp: 95%
    float delayTimeVal = params[RATE_PARAM].getValue();

    // Dynamically calculate delay line tap length (Delay range: 50ms - 1.5s)
    int delaySamps = (int)((0.05f + delayTimeVal * 1.45f) * sampleRate);

    // Write to circular delay buffer
    fxUnit.delayBufferL[fxUnit.delayPtr] = inputBusL + (fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000] * fbackVal);
    fxUnit.delayBufferR[fxUnit.delayPtr] = inputBusR + (fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000] * fbackVal);

    float delayOutL = fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000];
    float delayOutR = fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000];

    fxUnit.delayPtr = (fxUnit.delayPtr + 1) % 48000;

    // --- PROCESSOR 2: SHIMMER REVERB TANK ---
    // (Note: Reverb State calculations are located inside the FX cpp files)

    // Sum dry with wet processed spatial signals relative to stereo output channels
    float wetL = (delayOutL * delaySendVal);
    float wetR = (delayOutR * delaySendVal);

    // Write final stereo master bus outputs
    for (int i = 0; i < 8; i++) {
        if (outputs[CH1_OUTPUT + i].isConnected()) {
            float drySignal = outputs[CH1_OUTPUT + i].getVoltage();
            
            float panL = 1.0f - ((float)i / 7.0f);
            float panR = (float)i / 7.0f;

            float finalOutL = drySignal + (wetL * panL * 5.0f);
            float finalOutR = drySignal + (wetR * panR * 5.0f);

            // Left output (CH 1–4) / Right output (CH 5–8) separation
            if (i < 4) {
                outputs[CH1_OUTPUT + i].setVoltage(finalOutL);
            } else {
                outputs[CH1_OUTPUT + i].setVoltage(finalOutR);
            }
        }
    }
}
