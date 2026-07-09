#pragma once
#include <rack.hpp>
#include "plugin.hpp"

using namespace rack;

struct Soundscapes : Module {
    enum ParamId {
        ROOT_PARAM, SCALE_PARAM, RATE_PARAM, DENSITY_PARAM, TIMBRE_PARAM, TEXTURE_PARAM, SPREAD_PARAM, DYNAMICS_PARAM,
        MODE_PARAM, // Added 3-way voice engine selector
        FM_PARAM, DELAY_PARAM, REVERB_PARAM, FILTER_PARAM,
        PLAY_PARAM, SHFT_PARAM, ARP_PARAM, FRZ_PARAM, CHRD_PARAM, PROB_PARAM, SAVE_PARAM, RCL_PARAM,
        FADER_1_PARAM, FADER_2_PARAM, FADER_3_PARAM, FADER_4_PARAM, FADER_5_PARAM, FADER_6_PARAM, FADER_7_PARAM, FADER_8_PARAM,
        PAD_1_PARAM, PAD_2_PARAM, PAD_3_PARAM, PAD_4_PARAM, PAD_5_PARAM, PAD_6_PARAM, PAD_7_PARAM, PAD_8_PARAM,
        PAD_9_PARAM, PAD_10_PARAM, PAD_11_PARAM, PAD_12_PARAM, PAD_13_PARAM, PAD_14_PARAM, PAD_15_PARAM, PAD_16_PARAM,
        PARAMS_LEN
    };
    enum InputId { CLK_INPUT, RST_INPUT, VOCT_INPUT, GATE_INPUT, VEL_INPUT, EXTIN_INPUT, DUCK_INPUT, INPUTS_LEN };
    enum OutputId { PORT_1_OUTPUT, PORT_2_OUTPUT, PORT_3_OUTPUT, PORT_4_OUTPUT, PORT_5_OUTPUT, PORT_6_OUTPUT, PORT_7_OUTPUT, PORT_8_OUTPUT, OUTPUTS_LEN };
    enum LightId {
        PORT_1_LIGHT, PORT_2_LIGHT, PORT_3_LIGHT, PORT_4_LIGHT, PORT_5_LIGHT, PORT_6_LIGHT, PORT_7_LIGHT, PORT_8_LIGHT,
        PAD_1_LIGHT, PAD_2_LIGHT, PAD_3_LIGHT, PAD_4_LIGHT, PAD_5_LIGHT, PAD_6_LIGHT, PAD_7_LIGHT, PAD_8_LIGHT,
        PAD_9_LIGHT, PAD_10_LIGHT, PAD_11_LIGHT, PAD_12_LIGHT, PAD_13_LIGHT, PAD_14_LIGHT, PAD_15_LIGHT, PAD_16_LIGHT,
        FM_LIGHT_BLUE, DELAY_LIGHT_BLUE, REVERB_LIGHT_BLUE, FILTER_LIGHT_BLUE,
        PLAY_LIGHT_GREEN, SHFT_LIGHT_YELLOW, ARP_LIGHT_BLUE, FRZ_LIGHT_BLUE, CHRD_LIGHT_PURPLE, PROB_LIGHT_PURPLE, SAVE_LIGHT_ORANGE, RCL_LIGHT_ORANGE,
        LIGHTS_LEN
    };

    // State machine tracking
    int focusedChannel = -1;
    int focusedFX = -1;
    bool shftMode = false;
    bool probMode = false;
    bool chordMode = false;
    bool playMode = false;
    bool saveMode = false;
    bool rclMode = false;
    bool arpMode = false;
    bool frzMode = false;
    bool fxActive[4] = {false};

    // Sequencer Storage Matrix
    float stepVelocity[8][16];
    float stepProbability[8][16];
    float stepPitch[8][16];
    int stepOctave[8][16];
    bool stepGate[8][16];
    bool stepChordGate[8][16];

    // Playback control registers
    float channelAmplitudes[8];
    float channelFxSends[4][8];
    int loopLength[8];
    float playheadPhases[8];
    int currentSteps[8];

    // Trigger Processors
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger fxTriggers[4];
    dsp::SchmittTrigger btnTriggers[8];
    dsp::SchmittTrigger padTriggers[16];

    // Dynamic Voice Registers
    struct Voice {
        float phase = 0.0f;
        float env = 0.0f;
        float filterState = 0.0f;
        float pitch = 0.0f;
        float noiseState = 0.0f;
        float delayBuffer[2048] = {0.0f};
        int writeIdx = 0;
        float dustTimer = 0.0f;
    } voices[8];

    // Stereo FX buffers
    struct DelayProcessor {
        float buffer[48000] = {0.0f};
        int writeIdx = 0;
    } delayLineL, delayLineR;

    struct ReverbProcessor {
        float bufferL[24000] = {0.0f};
        float bufferR[24000] = {0.0f};
        int idxL = 0, idxR = 0;
    } reverbUnit;

    float duckEnv = 1.0f;

    Soundscapes();

    void process(const ProcessArgs& args) override;
    void toggleChannelFocus(int channelId);
    
    void processSequencer(const ProcessArgs& args);
    float processSynthVoice(int chan, const ProcessArgs& args);
    void processFXAndOutputs(float* drySignals, const ProcessArgs& args);
};
