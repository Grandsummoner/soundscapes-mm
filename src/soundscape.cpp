#include "soundscapes.hpp"

Soundscapes::Soundscapes() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    // Config default parameters
    configParam(ROOT_PARAM, 0.f, 11.f, 0.f, "Global Key Root");
    configParam(SCALE_PARAM, 0.f, 7.f, 0.f, "Global Scale Quantizer");
    configParam(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate / BPM");
    configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density / Detune");
    configParam(TIMBRE_PARAM, 0.f, 1.f, 0.5f, "Timbre / Filter Cutoff");
    configParam(TEXTURE_PARAM, 0.f, 1.f, 0.5f, "Texture / Grit");
    configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Spread / Loop Length");
    configParam(DYNAMICS_PARAM, 0.f, 1.f, 0.5f, "Dynamics / Decay");
    configParam(MODE_PARAM, 0.f, 2.f, 0.f, "Synthesizer Mode");

    // Initialize sequencer memory blocks
    for (int chan = 0; chan < 8; chan++) {
        loopLength[chan] = 16;
        currentSteps[chan] = 0;
        playheadPhases[chan] = 0.0f;
        channelAmplitudes[chan] = 0.8f;
        for (int s = 0; s < 16; s++) {
            stepVelocity[chan][s] = 0.8f;
            stepProbability[chan][s] = 1.0f;
            stepPitch[chan][s] = 0.0f;
            stepOctave[chan][s] = 4;
            stepGate[chan][s] = (s % 2 == 0); 
            stepChordGate[chan][s] = (s % 4 == 0);
        }
        for (int fx = 0; fx < 4; fx++) {
            channelFxSends[fx][chan] = 0.2f; 
        }
    }
}

void Soundscapes::toggleChannelFocus(int channelId) {
    if (focusedChannel == channelId) {
        focusedChannel = -1; 
    } else {
        focusedChannel = channelId;
    }
}

void Soundscapes::process(const ProcessArgs& args) {
    processSequencer(args);

    float drySignals[8] = {0.0f};
    for (int i = 0; i < 8; i++) {
        drySignals[i] = processSynthVoice(i, args);
    }

    processFXAndOutputs(drySignals, args);
}
