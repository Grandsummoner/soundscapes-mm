#include "soundscapes.hpp"

float Soundscapes::processSynthVoice(int chan, const ProcessArgs& args) {
    Voice& v = voices[chan];
    int activeStep = currentSteps[chan];
    
    bool hasGate = stepGate[chan][activeStep];
    float triggerProbability = stepProbability[chan][activeStep];
    bool triggerThisStep = hasGate && (random::uniform() <= triggerProbability);

    float rootVal = params[ROOT_PARAM].getValue();
    float noteOffset = stepPitch[chan][activeStep];
    float frequency = 261.63f * pow(2.0f, (rootVal + noteOffset) / 12.f);

    float outputSample = 0.0f;
    int synthMode = (int)params[MODE_PARAM].getValue(); // Evaluates MODE_PARAM

    if (synthMode == 0) {
        // --- MODE V: VOICES ---
        v.phase += frequency * args.sampleTime;
        if (v.phase >= 1.0f) v.phase -= 1.0f;

        float saw = 2.0f * v.phase - 1.0f;
        if (triggerThisStep) v.env = 1.0f;
        float decay = params[DYNAMICS_PARAM].getValue() * 1.5f + 0.05f;
        v.env -= (1.0f / (decay * args.sampleRate));
        if (v.env < 0.0f) v.env = 0.0f;

        float cutoff = params[TIMBRE_PARAM].getValue() * v.env;
        v.filterState += cutoff * (saw - v.filterState);
        outputSample = v.filterState * v.env;

    } else if (synthMode == 1) {
        // --- MODE W: WAVES ---
        int delaySamps = (int)(args.sampleRate / frequency);
        if (delaySamps > 2048) delaySamps = 2048;

        if (triggerThisStep) {
            for (int i = 0; i < 150; i++) {
                v.delayBuffer[i] = random::uniform() * 2.0f - 1.0f;
            }
        }

        float delayedVal = v.delayBuffer[v.writeIdx];
        float damping = params[TIMBRE_PARAM].getValue() * 0.49f + 0.5f;
        float nextVal = delayedVal * damping;
        v.delayBuffer[v.writeIdx] = nextVal;
        
        v.writeIdx = (v.writeIdx + 1) % delaySamps;
        outputSample = nextVal;

    } else {
        // --- MODE D: DRONE & DUST ---
        v.phase += (frequency * 0.5f) * args.sampleTime;
        if (v.phase >= 1.0f) v.phase -= 1.0f;
        float droneSignal = sin(2.0f * M_PI * v.phase);

        v.dustTimer -= args.sampleTime;
        float dustSpark = 0.0f;
        if (v.dustTimer <= 0.0f) {
            float density = params[DENSITY_PARAM].getValue() * 50.0f + 1.0f;
            v.dustTimer = (1.0f / density) * random::uniform();
            dustSpark = random::uniform() * 2.0f - 1.0f;
        }

        outputSample = (droneSignal * 0.6f) + (dustSpark * 0.4f);
    }

    return outputSample;
}
