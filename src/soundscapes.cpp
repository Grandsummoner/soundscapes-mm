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

    // 3. Configure Row 1 Channel Output Sockets (6 synth voices + stereo master sum,
    // occupying the physical jack positions that used to be channels 7/8)
    for (int i = 0; i < 6; i++) {
        configOutput(CH1_OUTPUT + i, string::f("Channel %d Audio Output", i + 1));
    }
    configOutput(MASTER_L_OUTPUT, "Master Mix Left Output");
    configOutput(MASTER_R_OUTPUT, "Master Mix Right Output");

    // 4. Configure Row 2 Core Synth Parameters
    configParam(MODE_PARAM, 0.0f, 2.0f, 0.0f, "Synthesis Mode Select (Voices -> Waves -> Drone & Dust)");
    configParam(COMPRESSOR_PARAM, 0.0f, 1.0f, 0.0f, "Compressor+Tilt EQ+Mid-Side Send");
    configParam(DELAY_PARAM, 0.0f, 1.0f, 0.0f, "Delay Send Edit Select");
    configParam(REVERB_PARAM, 0.0f, 1.0f, 0.0f, "Reverb Send Edit Select");
    configParam(FILTER_PARAM, 0.0f, 1.0f, 0.0f, "Filter Send Edit Select");

    configParam(RATE_PARAM, 0.0f, 1.0f, 0.5f, "Macro 1: Sequencer Rate / Delay Time");
    configParam(DENSITY_PARAM, 0.0f, 1.0f, 0.5f, "Macro 2: Voices Unison Thickness / Noise Dust Rate / Filter Resonance");
    configParam(TIMBRE_PARAM, 0.0f, 1.0f, 0.5f, "Macro 3: FM Harmonic Ratio / Reverb Shimmer Pitch");
    configParam(TEXTURE_PARAM, 0.0f, 1.0f, 0.5f, "Macro 4: FM Index Modulation / Filter Cutoff");
    configParam(SPREAD_PARAM, 0.0f, 1.0f, 0.5f, "Macro 5: Release Time / Stereo Spatial Detune / Delay Feedback");
    configParam(DYNAMICS_PARAM, 0.0f, 1.0f, 0.5f, "Macro 6: Attack Time / Filter Envelope / Reverb Decay");

    // 5. Configure Row 3 Mixer Faders & Center Quantizers
    for (int i = 0; i < 6; i++) {
        configParam(FADER1_PARAM + i, 0.0f, 1.0f, 0.8f, string::f("Channel %d Amplitude/Send Fader", i + 1));
    }
    configParam(FX_RETURN_PARAM, 0.0f, 1.0f, 0.5f, "FX Return: Global Wet Depth");
    configParam(MASTER_LEVEL_PARAM, 0.0f, 1.0f, 0.8f, "Master Level: Output Trim");
    configParam(ROOT_PARAM, 0.0f, 1.0f, 0.0f, "Diatonic Quantizer: Root Scale Transposition Note");
    configParam(SCALE_PARAM, 0.0f, 1.0f, 0.0f, "Diatonic Quantizer: Selected Harmonised Scale Degrees");
    configParam(WILDCARD_X_PARAM, 0.0f, 1.0f, 0.5f, "Wildcard Transpose: Reach/Wildness");
    configParam(WILDCARD_Y_PARAM, 0.0f, 1.0f, 0.5f, "Wildcard Transpose: Voice Balance (Bass vs Others)");

    // 6. Configure Row 4 Step Buttons (unified 16-step row, was 8 melody + 8 chord).
    // In normal operation these are passive playhead/occupied-slot indicators, not
    // click-to-toggle -- they only become interactive (as a slot picker) while SAVE
    // or RCL is armed. See StepPadWidget::onButton.
    for (int i = 0; i < 16; i++) {
        configParam(STEP_PARAM_START + i, 0.0f, 1.0f, 0.0f, string::f("Step Pad %d / Memory Slot %d", i + 1, i + 1));
    }

    // Performance section: 4 buttons (was 8 -- PLAY/SHFT/ARP/FRZ retired: PLAY
    // because the module free-runs by default and RATE-down-to-zero already
    // serves as "stop"; SHFT/ARP/FRZ were never wired to anything audible).
    configParam(PITCH_PARAM, 0.0f, 1.0f, 0.0f, "Arm Faders: Live-Record Step Pitch");
    configParam(PROB_PARAM, 0.0f, 1.0f, 0.0f, "Arm Faders: Live-Record Step Probability");
    configParam(SAVE_PARAM, 0.0f, 1.0f, 0.0f, "Save Pattern to Memory Slot (pick a step pad)");
    configParam(RCL_PARAM, 0.0f, 1.0f, 0.0f, "Recall Pattern from Memory Slot (browse step pads)");

    // Clear and zero out voice buffers to guarantee clean startup
    for (int i = 0; i < 6; i++) {
        voices[i].writeIdx = 0;
        voices[i].phase = 0.0f;
        voices[i].env = 0.0f;
        voices[i].noiseState = 0.0f;
        voices[i].opEnv = 0.0f;
        voices[i].modPhase = 0.0f;
        voices[i].unisonWriteIdx = 0;
        for (int b = 0; b < 2048; b++) {
            voices[i].unisonDelayBuffer[b] = 0.0f;
        }
        voices[i].subPhase = 0.0f;
        voices[i].svfLow = 0.0f;
        voices[i].svfBand = 0.0f;
        for (int b = 0; b < 2048; b++) {
            voices[i].delayBuffer[b] = 0.0f;
        }
    }

    // Seed a modest default pattern so the module isn't silent on first load, before
    // the player has recorded anything via PITCH/PROB fader-riding: alternating
    // steps at a comfortable mid-probability, pitch centered (quantizer default).
    for (int ch = 0; ch < 6; ch++) {
        for (int s = 0; s < 16; s++) {
            stepPitch[ch][s] = 0.5f;
            stepProb[ch][s] = (s % 2 == 0) ? 0.7f : 0.0f;
        }
    }

    // Clear FX Unit circular line buffers
    fxUnit.delayPtr = 0;
    fxUnit.reverbState = 0.0f;
    for (int b = 0; b < 48000; b++) {
        fxUnit.delayBufferL[b] = 0.0f;
        fxUnit.delayBufferR[b] = 0.0f;
    }

}
