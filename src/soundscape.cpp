#include "soundscapes.hpp"

Soundscapes::Soundscapes() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    // [Refer to soundscapes.hpp for Param configs...]
    
    // Initialize sequencer memory blocks
    for (int c = 0; chan < 8; chan++) {
        loopLength[chan] = 16;
        currentSteps[chan] = 0;
        playheadPhases[chan] = 0.0f;
        channelAmplitudes[chan] = 0.8f;
        for (int s = 0; s < 16; s++) {
            stepVelocity[chan][s] = 0.8f;
            stepProbability[chan][s] = 1.0f;
            stepPitch[chan][s] = 0.0f;
            stepOctave[chan][s] = 4;
            stepGate[chan][s] = (s % 2 == 0); // Default alternating triggers
            stepChordGate[chan][s] = (s % 4 == 0);
        }
        for (int fx = 0; fx < 4; fx++) {
            channelFxSends[fx][chan] = 0.2f; // Default subtle send
        }
    }
}

void Soundscapes::toggleChannelFocus(int channelId) {
    if (focusedChannel == channelId) {
        focusedChannel = -1; // Unfocus
    } else {
        focusedChannel = channelId;
    }
}

void Soundscapes::process(const ProcessArgs& args) {
    // 1. Compute Sequencer advancing and record live parameter locks
    processSequencer(args);

    // 2. Drive the 8 synthesis engines in parallel
    float drySignals[8] = {0.0f};
    for (int i = 0; i < 8; i++) {
        drySignals[i] = processSynthVoice(i, args);
    }

    // 3. Process Cascading FX and distribute to outputs
    processFXAndOutputs(drySignals, args);
}

Model* modelSoundscapes = createModel<Soundscapes, SoundscapesWidget>("soundscapes-mm");
