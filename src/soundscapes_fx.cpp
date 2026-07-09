#include "soundscapes.hpp"

void Soundscapes::processFXAndOutputs(float* drySignals, const ProcessArgs& args) {
    // 1. Sidechain DUCK Processing
    if (inputs[DUCK_INPUT].isConnected()) {
        float duckIn = inputs[DUCK_INPUT].getVoltage();
        float attack = 1000.f * args.sampleTime;
        float release = (params[DYNAMICS_PARAM].getValue() * 50.0f + 5.0f) * args.sampleTime;
        
        if (duckIn > duckEnv) {
            duckEnv += attack * (duckIn - duckEnv);
        } else {
            duckEnv -= release * duckEnv;
        }
    } else {
        duckEnv = 0.0f; 
    }

    float sidechainScalar = 1.0f - clamp(duckEnv / 10.0f, 0.0f, 0.95f);

    // 2. Cascade signals and process Reverb Sends
    float reverbInL = 0.0f;
    float reverbInR = 0.0f;

    for (int chan = 0; chan < 8; chan++) {
        float drySig = drySignals[chan];
        reverbInL += drySig * channelFxSends[2][chan];
        reverbInR += drySig * channelFxSends[2][chan];

        float finalOutput = drySig * sidechainScalar * channelAmplitudes[chan];
        outputs[PORT_1_OUTPUT + chan].setVoltage(finalOutput * 5.0f);
    }

    // 3. Process Stereo Reverb Tank
    reverbUnit.bufferL[reverbUnit.idxL] = reverbInL + (reverbUnit.bufferL[(reverbUnit.idxL + 12000) % 24000] * 0.8f);
    reverbUnit.bufferR[reverbUnit.idxR] = reverbInR + (reverbUnit.bufferR[(reverbUnit.idxR + 13400) % 24000] * 0.8f);

    reverbUnit.idxL = (reverbUnit.idxL + 1) % 24000;
    reverbUnit.idxR = (reverbUnit.idxR + 1) % 24000;
}
