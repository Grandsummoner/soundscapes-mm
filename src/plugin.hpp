#pragma once

#include "rack.hpp"

using namespace rack;

// Forward declare the plugin instance
extern Plugin* pluginInstance;

/**
 * Soundscapes Coordinate Constants (Singular Source of Truth)
 * Mathematically spaced to provide a breathing layout
 */
namespace SoundscapesCoords {
    // Left Sidebar Inputs
    const float SIDEBAR_JACK_X = 22.5f;
    const float SIDEBAR_LED_X = 38.5f; // Spacious gap between jack and LED
    const float SIDEBAR_Y_START = 55.0f;
    const float SIDEBAR_Y_SPACING = 47.0f;

    // Row 1: Expanded 8-Channel Outputs & Displays (44px spacing)
    const float CH_COLS[8] = {79.0f, 123.0f, 167.0f, 211.0f, 254.0f, 298.0f, 342.0f, 386.0f};
    const float ROW1_JACK_Y = 55.0f;
    const float ROW1_LED_Y = 74.0f;       // Spacious gap between jack and LED
    const float ROW1_DISPLAY_Y = 115.0f;  // Aligned perfectly with SVG backplate Y=115

    // Row 2: Centralized Synth Deck
    const float MODE_X = 79.0f;
    const float MODE_Y = 170.0f;
    const float FX_COLS[2] = {113.0f, 147.0f};
    const float FX_ROWS[2] = {163.0f, 189.0f};
    const float KNOB_COLS[6] = {181.0f, 222.0f, 263.0f, 304.0f, 345.0f, 386.0f};
    const float ROW2_KNOB_Y = 170.0f;

    // Row 3 & 4: 10-Column Grid (34px spacing)
    const float GRID_COLS[10] = {79.0f, 113.0f, 147.0f, 181.0f, 215.0f, 249.0f, 283.0f, 317.0f, 351.0f, 386.0f};
    const float ROW3_FADER_Y = 233.0f;
    const float ROOT_Y = 220.0f;
    const float SCALE_Y = 246.0f;

    // Row 4: Step Sequencer Pads & Buttons
    const float ROW4_MELODY_PAD_Y = 288.0f;
    const float ROW4_CHORD_PAD_Y = 336.0f;
    const float ROW4_BUTTON_ROWS[4] = {275.0f, 294.0f, 313.0f, 332.0f};
}

struct StepData {
    uint8_t note = 0;         // Midi pitch offset
    uint8_t velocity = 100;   // 0 - 127
    uint8_t probability = 100; // 0 - 100% trigger chance
    bool active = false;      // True if step is active
};

struct SequencerTrack {
    StepData steps[8];        // 8 contiguous steps
    int playhead = 0;         // Active playhead step
};

enum SynthMode {
    MODE_VOICES,       // "V" - Dual-Operator FM with virtual LPGs
    MODE_WAVES,        // "W" - Karplus-Strong string waveguide resonators
    MODE_DRONE_DUST    // "D" - Granular dust cloud + drone pads
};

enum FaderState {
    FADER_MIXER,       // Global mixer mode
    FADER_FM_SEND,     // Send level to FM modulation bus
    FADER_DELAY_SEND,  // Send level to delay processor
    FADER_REVERB_SEND, // Send level to shimmer reverb tank
    FADER_FILTER_SEND, // Send level to SVF filter
    FADER_VEL_FOCUS,   // Focused channel step velocity offset (SHFT OFF)
    FADER_PROB_FOCUS   // Focused channel step probability trigger weight (SHFT ON)
};

struct Soundscapes : Module {
    enum ParamId {
        MODE_PARAM,
        FM_PARAM,
        DELAY_PARAM,
        REVERB_PARAM,
        FILTER_PARAM,
        RATE_PARAM,
        DENSITY_PARAM,
        TIMBRE_PARAM,
        TEXTURE_PARAM,
        SPREAD_PARAM,
        DYNAMICS_PARAM,
        FADER1_PARAM, FADER2_PARAM, FADER3_PARAM, FADER4_PARAM,
        FADER5_PARAM, FADER6_PARAM, FADER7_PARAM, FADER8_PARAM,
        ROOT_PARAM,
        SCALE_PARAM,
        PLAY_PARAM, SHFT_PARAM,
        ARP_PARAM, FRZ_PARAM,
        CHRD_PARAM, PROB_PARAM,
        SAVE_PARAM, RCL_PARAM,
        STEP_PARAM_START,
        STEP_PARAM_END = STEP_PARAM_START + 16,
        NUM_PARAMS
    };

    enum InputId {
        CLK_INPUT,
        RST_INPUT,
        VOCT_INPUT,
        GATE_INPUT,
        VEL_INPUT,
        EXT_INPUT,
        DUCK_INPUT,
        NUM_INPUTS
    };

    enum OutputId {
        CH1_OUTPUT, CH2_OUTPUT, CH3_OUTPUT, CH4_OUTPUT,
        CH5_OUTPUT, CH6_OUTPUT, CH7_OUTPUT, CH8_OUTPUT,
        NUM_OUTPUTS
    };

    enum LightId {
        CLK_LED, RST_LED, VOCT_LED, GATE_LED, VEL_LED, EXT_LED, DUCK_LED,
        CH1_LED, CH2_LED, CH3_LED, CH4_LED, CH5_LED, CH6_LED, CH7_LED, CH8_LED,
        STEP_LED_START,
        STEP_LED_END = STEP_LED_START + 16,
        PLAY_LED, SHFT_LED, ARP_LED, FRZ_LED, CHRD_LED, PROB_LED, SAVE_LED, RCL_LED,
        NUM_LIGHTS
    };

    // Sequencer & Latching State
    SequencerTrack melodyTrack;
    SequencerTrack chordTrack;
    
    int focusedChannel = -1;       // Range: -1 (Global Mixer) to 0-7 (Focus 1–8)
    bool shiftActive = false;      // SHFT toggle switch state
    bool isPlaying = true;         // Transport state
    SynthMode activeSynthMode = MODE_VOICES;
    FaderState activeFaderState = FADER_MIXER;

    // Flashing display clock trackers for UI
    float flashTimer = 0.0f;
    bool displayFlashState = false;

    // Dynamic value display HUD arrays
    float displayValue[8] = {};
    float displayValueTimer[8] = {};
    int displayType[8] = {};       // 0: Percentage, 1: Root Note, 2: Scale Type

    // Sequencer Timing & Probability tracking
    bool voiceTriggerActive[8] = {};
    bool chordTriggerActive[8] = {};
    float stepTimeElapsed = 0.0f;  // Keeps track of physical elapsed step duration in seconds

    // DSP Processing Variables
    float channelVolumes[8] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
    float fxSends[4][8]; // [FM, Delay, Reverb, Filter] x [CH1-8]
    
    struct VoiceDSP {
        float phase = 0.0f;
        float freq = 220.0f;
        float env = 0.0f;
        float delayBuffer[2048] = {0.0f};
        int writeIdx = 0;
        float noiseState = 0.0f;
        void trigger() { env = 1.0f; }
    } voices[8];

    struct FXTank {
        float delayBufferL[48000] = {0.0f};
        float delayBufferR[48000] = {0.0f};
        int delayPtr = 0;
        float reverbState = 0.0f;
        float sidechainEnv = 0.0f;
    } fxUnit;

    Soundscapes();
    void process(const ProcessArgs& args) override;
    void processSequencer(float sampleTime);
    void processDSP(const ProcessArgs& args);
    void handleFocusToggle(int channel);
    void handleFaderMapping();
    void initializeSequence();
};
