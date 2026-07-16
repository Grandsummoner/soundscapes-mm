#pragma once

#include "rack.hpp"
#include <cmath>

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
    const float SIDEBAR_LED_X = 41.0f; // Small gap between jack and LED
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
    const float SCALE_Y = 238.0f; // was 246 -- raised 8px so the label (now same
                                   // size as the 6 big-knob labels) clears the
                                   // performance button block below it

    // Row 4: Step Sequencer Pads & Buttons
    const float ROW4_MELODY_PAD_Y = 288.0f;
    const float ROW4_CHORD_PAD_Y = 336.0f;
    const float ROW4_BUTTON_ROWS[2] = {285.0f, 313.0f}; // 2 rows x 2 cols = 4
                                                          // buttons (PITCH, PROB,
                                                          // SAVE, RCL); was 4 rows
                                                          // of 8 buttons
}

struct StepData {
    uint8_t note = 0;         // Midi pitch offset
    uint8_t velocity = 100;   // 0 - 127
    uint8_t probability = 100; // 0 - 100% trigger chance
    bool active = false;      // True if step is active
    int8_t targetChannel = -1; // Which channel (0-5) this step triggers; -1 = "own index"
                                // (set at construction to match the step's own index by
                                // default, but reassignable via SHFT+click while a channel
                                // is focused, decoupling timing position from harmonic role)
    bool noteOverride = false;   // If true, this step plays its own scale degree instead
    int8_t noteDegreeOffset = 0;  // of its target channel's fixed harmonic role -- lets a
                                   // step carry real melodic movement (set via NOTE mode,
                                   // the PROB button, cycling 0-13 then clearing on wrap)
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
        FADER5_PARAM, FADER6_PARAM,
        FX_RETURN_PARAM,    // Global fader, col 7: FX wet-return depth
        MASTER_LEVEL_PARAM, // Global fader, col 8: Master L/R output trim
        ROOT_PARAM,
        SCALE_PARAM,
        PLAY_PARAM, SHFT_PARAM,
        ARP_PARAM, FRZ_PARAM,
        CHRD_PARAM, PROB_PARAM,
        SAVE_PARAM, RCL_PARAM,
        STEP_PARAM_START,
        STEP_PARAM_END = STEP_PARAM_START + 16, // 8 melody + 8 chord
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
        CH5_OUTPUT, CH6_OUTPUT,
        MASTER_L_OUTPUT, MASTER_R_OUTPUT, // Occupy the physical jack positions that used
                                           // to be channels 7/8 -- 6 synth voices plus a
                                           // dedicated stereo sum, no external mixer
                                           // required for the "just give me the mix" case
        NUM_OUTPUTS
    };

    enum LightId {
        CLK_LED, RST_LED, VOCT_LED, GATE_LED, VEL_LED, EXT_LED, DUCK_LED,
        CH1_LED, CH2_LED, CH3_LED, CH4_LED, CH5_LED, CH6_LED,
        MASTER_L_LED, MASTER_R_LED,
        STEP_LED_START,
        STEP_LED_END = STEP_LED_START + 16, // 8 melody + 8 chord
        PLAY_LED, SHFT_LED, ARP_LED, FRZ_LED, CHRD_LED, PROB_LED, SAVE_LED, RCL_LED,
        NUM_LIGHTS
    };

    // Sequencer & Latching State
    SequencerTrack melodyTrack;
    SequencerTrack chordTrack;

    int focusedChannel = -1;       // Range: -1 (Global Mixer) to 0-5 (Focus 1-6)
    bool shiftActive = false;      // SHFT toggle switch state
    bool isPlaying = true;         // Transport state
    SynthMode activeSynthMode = MODE_VOICES;
    FaderState activeFaderState = FADER_MIXER;

    // When a macro knob is turned, its current function name is spelled across all 8
    // 7-segment displays (one character each) so its role is unambiguous regardless of
    // which mode/patch state it's currently operating under.
    char macroFunctionText[9] = "";
    bool macroFunctionActive = false;

    // Determines whether a macro knob (RATE..DYNAMICS) currently has any audible
    // effect, given the active synth mode, EXT IN connection, and FX send levels --
    // used to visually distinguish "in use" knobs from inert ones on the panel.
    bool isMacroActive(int paramId) {
        bool reverbSendUp = params[REVERB_PARAM].getValue() > 0.001f;

        switch (paramId) {
            case RATE_PARAM:
                return true; // Always drives sequencer step timing (and delay time)
            case DENSITY_PARAM:
                // Always does something now: note density (Voices), string
                // unison/chorus (Waves), dust rate (Drone & Dust).
                return true;
            case TIMBRE_PARAM:
                // No Voices-mode job for now (EXT IN blending is on hold) -- only
                // active when the Reverb FX send is up (shimmer pitch-shift amount).
                return reverbSendUp;
            case TEXTURE_PARAM: {
                bool filterSendUp = params[FILTER_PARAM].getValue() > 0.001f;
                // Always active in Voices mode (VA waveshape), or when the Filter FX
                // send is up (cutoff).
                return (activeSynthMode == MODE_VOICES) || filterSendUp;
            }
            case SPREAD_PARAM:
            case DYNAMICS_PARAM:
                return true; // Always drive the shared Release/Attack envelope
            default:
                return true;
        }
    }

    // Returns which FX-bus accent color (if any) currently applies to a macro knob,
    // matching that FX button's color for easy visual pairing. 0 = none, 1 = FM,
    // 2 = DELAY, 3 = REVERB, 4 = FILTER.
    int macroAccentGroup(int paramId) {
        if (activeFaderState == FADER_DELAY_SEND && (paramId == RATE_PARAM || paramId == SPREAD_PARAM)) return 2;
        if (activeFaderState == FADER_REVERB_SEND && (paramId == TIMBRE_PARAM || paramId == DYNAMICS_PARAM)) return 3;
        if (activeFaderState == FADER_FILTER_SEND && paramId == TEXTURE_PARAM) return 4;
        return 0;
    }

    // Returns an up-to-8-character label describing what a macro knob is currently
    // doing, spelled one character per display across all 8 7-segment displays when
    // that knob is turned. Kept honest to the actual current implementation -- a knob
    // that's inert right now says so via isMacroActive(), not via a hopeful label.
    const char* macroFunctionName(int paramId) {
        switch (paramId) {
            case RATE_PARAM:
                return "RATE";
            case DENSITY_PARAM:
                if (activeSynthMode == MODE_VOICES) {
                    return "NOTEDENS";
                } else if (activeSynthMode == MODE_WAVES) {
                    return "CHORUS";
                } else {
                    return "DUSTRATE";
                }
            case TIMBRE_PARAM:
                return "TIMBRE";
            case TEXTURE_PARAM:
                return (activeSynthMode == MODE_VOICES) ? "OSCSHAPE" : "TEXTURE";
            case SPREAD_PARAM:
                return "RELEASE";
            case DYNAMICS_PARAM:
                return "ATTACK";
            default:
                return "";
        }
    }

    // CHRD mode: latching switch that repurposes the 16 step pads as a radio-select
    // option menu (mutually exclusive with normal step editing -- entering CHRD mode
    // freezes the step pattern rather than letting the two be edited simultaneously).
    // All 16 slots are currently just reserved framework -- nothing wired to them yet.
    bool chordModeActive = false;
    int chordModeOption = -1;              // -1 = none selected, else 0-15

    // NOTE mode (PROB button, latching): while active, clicking a step pad cycles
    // that step's own melodic override (0-13 scale degrees, then clears back to
    // "use the channel's fixed harmonic role" on wrap) instead of toggling it on/off.
    bool noteModeActive = false;

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
    float channelVolumes[6] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
    float fxSends[4][6]; // [FM, Delay, Reverb, Filter] x [CH1-6]
    
    struct VoiceDSP {
        float phase = 0.0f;         // Carrier phase (Voices VA osc) / drone phase (Drone & Dust)
        float freq = 220.0f;
        float env = 0.0f;           // Amplitude envelope
        float delayBuffer[2048] = {0.0f};
        int writeIdx = 0;
        float noiseState = 0.0f;    // LPG smoothing filter state (Waves/Drone-Dust use)
        void trigger() { env = 1.0f; }

        // --- Voices mode (VA osc + FM/ring-mod) specific state ---
        float opEnv = 0.0f;         // Independent operator/brightness envelope
        float modPhase = 0.0f;      // Modulator phase accumulator (EXT IN ring-mod, Voices mode)
        float unisonDelayBuffer[2048] = {0.0f}; // Detuned 2nd string voice (DENSITY chorus, Waves mode)
        int unisonWriteIdx = 0;
        float subPhase = 0.0f;      // Sub-oscillator phase (bass anchor voice only)
        float svfLow = 0.0f;        // Per-voice resonant filter state (lowpass)
        float svfBand = 0.0f;       // Per-voice resonant filter state (bandpass)
    } voices[6];

    struct FXTank {
        float delayBufferL[48000] = {0.0f};
        float delayBufferR[48000] = {0.0f};
        int delayPtr = 0;
        float reverbState = 0.0f;
        float sidechainEnv = 0.0f;
    } fxUnit;

    /**
     * Andrew Simper/Trapezoidal Linear State-Variable Filter (SVF)
     * (Per-instance member -- previously a file-scope static shared by every
     * Soundscapes module in the whole VCV Rack session, and persisting for the
     * life of the process even if the module was deleted and recreated.)
     */
    struct StereoSVF {
        float ic1eq_L = 0.0f, ic2eq_L = 0.0f;
        float ic1eq_R = 0.0f, ic2eq_R = 0.0f;

        void process(float inputL, float inputR, float cutoffHz, float resonance, float sampleRate, float& outL, float& outR) {
            cutoffHz = math::clamp(cutoffHz, 20.0f, 18000.0f);
            resonance = math::clamp(resonance, 0.01f, 0.99f);
            float q = 1.0f / (2.0f * (1.0f - resonance));

            float g = std::tan(M_PI * cutoffHz / sampleRate);
            float k = 1.0f / q;
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;

            float v3_L = inputL - ic2eq_L;
            float v1_L = a1 * ic1eq_L + a2 * v3_L;
            float v2_L = ic2eq_L + g * v1_L;
            ic1eq_L = 2.0f * v1_L - ic1eq_L;
            ic2eq_L = 2.0f * v2_L - ic2eq_L;
            outL = v2_L;

            float v3_R = inputR - ic2eq_R;
            float v1_R = a1 * ic1eq_R + a2 * v3_R;
            float v2_R = ic2eq_R + g * v1_R;
            ic1eq_R = 2.0f * v1_R - ic1eq_R;
            ic2eq_R = 2.0f * v2_R - ic2eq_R;
            outR = v2_R;

            // Defensive sanitization: a filter's internal state is fully recursive
            // (each sample depends on the last), so a single NaN/Inf sample -- from
            // any transient instability -- would otherwise poison every future
            // sample forever, even with resonance/cutoff back in normal range.
            if (!std::isfinite(ic1eq_L) || !std::isfinite(ic2eq_L)) { ic1eq_L = 0.0f; ic2eq_L = 0.0f; }
            if (!std::isfinite(ic1eq_R) || !std::isfinite(ic2eq_R)) { ic1eq_R = 0.0f; ic2eq_R = 0.0f; }
            if (!std::isfinite(outL)) outL = 0.0f;
            if (!std::isfinite(outR)) outR = 0.0f;
        }
    } mainFilter;

    /**
     * Circular-Buffer Pitch-Shifter (+1 Octave) for Shimmer Reverb Feedback Loop
     * (Per-instance member -- see StereoSVF note above.)
     */
    struct ShimmerPitchShifter {
        float buffer[4096] = {0.0f};
        int writePtr = 0;
        float readPtr1 = 0.0f;
        float readPtr2 = 2048.0f;

        float process(float sample) {
            if (!std::isfinite(sample)) sample = 0.0f;
            buffer[writePtr] = sample;

            readPtr1 += 2.0f;
            readPtr2 += 2.0f;

            if (readPtr1 >= 4096.0f) readPtr1 -= 4096.0f;
            if (readPtr2 >= 4096.0f) readPtr2 -= 4096.0f;

            int idx1 = (int)readPtr1;
            float frac1 = readPtr1 - idx1;
            float out1 = (1.0f - frac1) * buffer[idx1] + frac1 * buffer[(idx1 + 1) % 4096];

            int idx2 = (int)readPtr2;
            float frac2 = readPtr2 - idx2;
            float out2 = (1.0f - frac2) * buffer[idx2] + frac2 * buffer[(idx2 + 1) % 4096];

            float fade = std::abs(2048.0f - (float)writePtr) / 2048.0f;
            float shiftedOutput = out1 * fade + out2 * (1.0f - fade);

            writePtr = (writePtr + 1) % 4096;

            return std::isfinite(shiftedOutput) ? shiftedOutput : 0.0f;
        }
    } shimmerShiftL, shimmerShiftR;

    Soundscapes();
    void process(const ProcessArgs& args) override;
    void processSequencer(float sampleTime);
    void processDSP(const ProcessArgs& args);
    void handleFocusToggle(int channel);
    void handleFaderMapping();
    void initializeSequence();
};
