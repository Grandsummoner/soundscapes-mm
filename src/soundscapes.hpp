#pragma once

#include "rack.hpp"
#include <cmath>

// Exponential interpolation for time/frequency-style knobs: at raw=0 gives
// minVal, at raw=1 gives maxVal, with equal perceptual (octave-like) steps per
// unit of rotation -- most of the knob's travel covers short/musical values,
// only the extreme end reaches the slow/pad-like tail. Contrast with a plain
// linear knob, which spends most of its rotation on values nobody wants.
static inline float expMap(float raw, float minVal, float maxVal) {
    raw = rack::math::clamp(raw, 0.0f, 1.0f);
    return minVal * std::pow(maxVal / minVal, raw);
}

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
    const float ROW1_DISPLAY_Y = 100.0f;  // Moved nearer to the LED row (was 115,
                                           // a 41px gap from the LED at 74 -- now ~26px)

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
    const float SCALE_Y = 220.0f; // was 238 (diagonal from ROOT) -- now same row as
                                   // ROOT, freeing the space below for the
                                   // performance button block to stretch into

    // Row 4: Step Sequencer Pads & Buttons
    const float ROW4_MELODY_PAD_Y = 300.0f; // was 288 -- shifted down to use the bottom margin
    const float ROW4_CHORD_PAD_Y = 348.0f;  // was 336
    const float ROW4_BUTTON_ROWS[2] = {318.0f, 348.0f}; // was {306, 336} -- shifted down
}

// A single saved pattern: all 6 channels' pitch + probability across all 16 steps,
// captured together as one snapshot (a "slot" isn't meaningful per-channel, since
// all 6 channels share one playhead position -- see Soundscapes::stepPitch/stepProb
// below for why).
struct PatternSlot {
    bool occupied = false;
    float pitch[6][16] = {};
    float prob[6][16] = {};
};

// Crossfader Scene A/B: a snapshot of every "global" continuous control -- the 6
// channel faders, FX Return, Master Level, the 6 macro knobs, and Root/Scale.
// Deliberately does NOT include step pitch/probability data (per-step content
// morphing is a different thing from morphing continuous controls) or any
// button/latch state (on-off states don't have a meaningful "halfway").
struct SceneSnapshot {
    bool captured = false;
    float fader[6] = {};
    float fxReturn = 0.0f;
    float masterLevel = 0.0f;
    float macro[6] = {};
    float root = 0.0f;
    float scaleParam = 0.0f;
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
        COMPRESSOR_PARAM, // Was FM_PARAM -- FM never did anything audible (confirmed
                          // dead), repurposed for Compressor+Tilt EQ+Mid-Side
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
        // X-Y wildcard transpose pad (live performance, continuous -- no measure
        // count/timer): X = reach/wildness (center = tame, edges = bigger leaps
        // like 4ths/octaves more likely), Y = voice balance (center = balanced,
        // one direction = bass deepens while others hold, other direction =
        // reverse). Center (0.5, 0.5) = fully off.
        WILDCARD_X_PARAM, WILDCARD_Y_PARAM,
        // Octatrack-style crossfader: morphs between two captured global-state
        // snapshots (see SceneSnapshot below) -- press SCENE_A or SCENE_B to
        // capture the current fader/knob state into that end, then sliding
        // between them interpolates every captured param live.
        CROSSFADER_PARAM,
        SCENE_A_PARAM, SCENE_B_PARAM, // Momentary: press to capture current state
        // Performance section: 4 buttons (was 8). PITCH/PROB arm the channel faders
        // for live-record; SAVE/RCL repurpose the 16 step pads as a slot picker.
        // Exclusivity: SAVE forces PITCH/PROB/RCL off; RCL forces SAVE off (but
        // allows PITCH/PROB to combine with it); PITCH/PROB force SAVE off.
        PITCH_PARAM, PROB_PARAM,
        SAVE_PARAM, RCL_PARAM,
        STEP_PARAM_START,
        STEP_PARAM_END = STEP_PARAM_START + 16, // Unified 16-step row (was 8 melody + 8 chord)
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
        STEP_LED_END = STEP_LED_START + 16, // Unified 16-step row (was 8 melody + 8 chord)
        PITCH_LED, PROB_LED, SAVE_LED, RCL_LED,
        NUM_LIGHTS
    };

    // Sequencer & Latching State
    //
    // Shared timeline, independent per-channel content: all 6 channels advance
    // through the same 16-step playhead together, but each channel stores its own
    // pitch + probability at every step -- 6 parallel automation lanes over one
    // clock, not 6 separate sequencers. See stepPitch/stepProb below.
    int currentStep = 0;             // 0-15, shared playhead for all 6 channels
    float stepPitch[6][16] = {};     // Per-channel, per-step raw pitch (0-1, quantized downstream)
    float stepProb[6][16] = {};      // Per-channel, per-step trigger probability (0-1); doubles as the on/off flag -- 0 = effectively silent
    bool channelTriggerActive[6] = {}; // This pass's probability roll result, per channel, latched at each step advance

    PatternSlot slots[16];           // SAVE/RCL memory -- one slot = all 6 channels' full pattern

    SceneSnapshot sceneA, sceneB;     // Crossfader morph targets

    float channelWildcardOffset[6] = {}; // Semitone offset from the X-Y wildcard pad,
                                          // recomputed live each step advance -- a
                                          // transient performance layer on top of
                                          // the recorded stepPitch, not baked into it

    // PITCH/PROB: latching, multi-armable together. While armed, the channel
    // faders stop controlling amplitude and become live-record inputs instead --
    // riding a fader writes into that channel's own stepPitch/stepProb at
    // whatever step the shared clock is currently on (live-looper style: every
    // pass the fader is touched, it overwrites that step again).
    bool pitchArmed = false;
    bool probArmed = false;

    // SAVE: latching, but self-clears the instant a slot is picked (see
    // processSequencer). Exclusive with PITCH/PROB/RCL -- engaging SAVE forces
    // all three off.
    bool saveArmed = false;

    // RCL: latching, stays latched until pressed again (a "browse memory slots
    // while performing" mode) -- unlike SAVE, doesn't auto-clear on a pad press,
    // and can combine with PITCH/PROB so a loaded pattern can be tweaked live
    // without leaving RCL. Exclusive with SAVE only.
    bool rclArmed = false;

    int focusedChannel = -1;       // Range: -1 (Global Mixer) to 0-5 (Focus 1-6)
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
        bool filterSendUp = params[FILTER_PARAM].getValue() > 0.001f;
        bool compressorSendUp = params[COMPRESSOR_PARAM].getValue() > 0.001f;

        switch (paramId) {
            case RATE_PARAM:
                return true; // Always drives sequencer step timing (and delay time)
            case DENSITY_PARAM:
                // Always does something now: note density (Voices), string
                // unison/chorus (Waves), dust rate (Drone & Dust).
                return true;
            case TIMBRE_PARAM:
                // Shared across three exclusive FX buses (only one can ever be
                // active at once): Reverb (shimmer amount), Filter (resonance),
                // Compressor (comp+tilt strength).
                return reverbSendUp || filterSendUp || compressorSendUp;
            case TEXTURE_PARAM:
                // Always active in Voices mode (VA waveshape), or when Filter
                // (cutoff) or Compressor (mid-side width) is the active bus.
                return (activeSynthMode == MODE_VOICES) || filterSendUp || compressorSendUp;
            case SPREAD_PARAM:
            case DYNAMICS_PARAM:
                return true; // Always drive the shared Release/Attack envelope
            default:
                return true;
        }
    }

    // Returns which FX-bus accent color (if any) currently applies to a macro knob,
    // matching that FX button's color for easy visual pairing. 0 = none, 2 = DELAY,
    // 3 = REVERB, 4 = FILTER, 5 = COMPRESSOR.
    int macroAccentGroup(int paramId) {
        if (activeFaderState == FADER_DELAY_SEND && (paramId == RATE_PARAM || paramId == SPREAD_PARAM)) return 2;
        if (activeFaderState == FADER_REVERB_SEND && (paramId == TIMBRE_PARAM || paramId == DYNAMICS_PARAM)) return 3;
        if (activeFaderState == FADER_FILTER_SEND && (paramId == TEXTURE_PARAM || paramId == TIMBRE_PARAM)) return 4;
        if (activeFaderState == FADER_FM_SEND && (paramId == TIMBRE_PARAM || paramId == TEXTURE_PARAM)) return 5; // Compressor
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

    // Flashing display clock trackers for UI
    float flashTimer = 0.0f;
    bool displayFlashState = false;

    // Dynamic value display HUD arrays
    float displayValue[8] = {};
    float displayValueTimer[8] = {};
    int displayType[8] = {};       // 0: Percentage, 1: Root Note, 2: Scale Type

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
        bool prevGate = false;      // Rising-edge detection for Waves mode's pluck excitation
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
        float delayDampL = 0.0f;    // One-pole HF damping on the feedback tap --
        float delayDampR = 0.0f;    // repeats darken over time like a tape echo,
                                    // instead of staying full-bandwidth forever
        float compEnvelope = 0.0f;  // Compressor's RMS envelope follower
        float tiltLowL = 0.0f;      // Tilt EQ's one-pole low/high crossover split
        float tiltLowR = 0.0f;

        // Smoothed copies of each FX bus's send level, used only for the audio
        // math below (the raw params stay instant for the exclusivity/UI logic).
        // Fixes the volume jump when switching buses (e.g. Delay -> Filter): the
        // wet content used to snap from one bus's character to another's in a
        // single sample; now it crossfades over ~40ms instead.
        float smoothedDelaySend = 0.0f;
        float smoothedReverbSend = 0.0f;
        float smoothedFilterSend = 0.0f;
        float smoothedCompressorSend = 0.0f;
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
    void handleSceneMorph();
};
