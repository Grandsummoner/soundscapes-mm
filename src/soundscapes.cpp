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

    // Clear and zero out voice buffers to guarantee clean startup
    for (int i = 0; i < 8; i++) {
        voices[i].writeIdx = 0;
        voices[i].phase = 0.0f;
        voices[i].env = 0.0f;
        voices[i].noiseState = 0.0f;
        voices[i].opEnv = 0.0f;
        voices[i].modPhase = 0.0f;
        voices[i].fbState = 0.0f;
        voices[i].subPhase = 0.0f;
        for (int b = 0; b < 2048; b++) {
            voices[i].delayBuffer[b] = 0.0f;
        }
    }

    // Clear FX Unit circular line buffers
    fxUnit.delayPtr = 0;
    fxUnit.reverbState = 0.0f;
    for (int b = 0; b < 48000; b++) {
        fxUnit.delayBufferL[b] = 0.0f;
        fxUnit.delayBufferR[b] = 0.0f;
    }

    // Initialize Default Sequencer Trigger States
    initializeSequence();
}
