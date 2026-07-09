#include "soundscapes.hpp"

void Soundscapes::processFXAndOutputs(float* drySignals, const ProcessArgs& args) {
    // 1. Process Ducking Sidechain Envelope
    if (inputs[DUCK_INPUT].isConnected()) {
        float duckIn = inputs[DUCK_INPUT].getVoltage();
        // Envelope Follower tracking duck CV
        float attack = 1000.f * args.sampleTime;
        float release = (params[DYNAMICS_PARAM].getValue() * 50.0f + 5.0f) * args.sampleTime;
        
        if (duckIn > duckEnv) {
            duckEnv += attack * (duckIn - duckEnv);
        } else {
            duckEnv -= release * duckEnv;
        }
    } else {
        duckEnv = 0.0f; // No attenuation
    }

    // Apply scaling factor to ducking amplitude
    float sidechainScalar = 1.0f - clamp(duckEnv / 10.0f, 0.0f, 0.95f);

    // 2. Cascade Dry Signals to Outputs and Collect Send Signals
    float reverbInL = 0.0f;
    float reverbInR = 0.0f;

    for (int chan = 0; chan < 8; chan++) {
        float drySig = drySignals[chan];
        
        // Accumulate Reverb Send signals
        reverbInL += drySig * channelFxSends[2][chan];
        reverbInR += drySig * channelFxSends[2][chan];

        // Attenuate physical output by sidechain scalar
        float finalOutput = drySig * sidechainScalar;
        outputs[PORT_1_OUTPUT + chan].setVoltage(finalOutput * 5.0f);
    }

    // 3. The Reverb / Shimmer Reflection Tank (Simple Feedback delay network)
    reverbUnit.bufferL[reverbUnit.idxL] = reverbInL + (reverbUnit.bufferL[(reverbUnit.idxL + 12000) % 24000] * 0.8f);
    reverbUnit.bufferR[reverbUnit.idxR] = reverbInR + (reverbUnit.bufferR[(reverbUnit.idxR + 13400) % 24000] * 0.8f);

    reverbUnit.idxL = (reverbUnit.idxL + 1) % 24000;
    reverbUnit.idxR = (reverbUnit.idxR + 1) % 24000;

    // Output final spatial signal onto Master Stereo returns (if unpatched)
    // [Note: Reverb signals are mixed back locally in your VCV mixing node]
}
