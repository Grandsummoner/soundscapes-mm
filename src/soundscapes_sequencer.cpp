#include "soundscapes.hpp"

void Soundscapes::processSequencer(const ProcessArgs& args) {
    // 1. Process Global Buttons State Toggles
    if (btnTriggers[1].process(params[SHFT_PARAM].getValue())) {
        shftMode = !shftMode;
        if (shftMode) focusedFX = -1; // Symmetrical disengage
    }
    if (btnTriggers[0].process(params[PLAY_PARAM].getValue())) {
        if (shftMode) {
            // SHFT + PLAY = Reset playheads
            for(int i = 0; i < 8; i++) {
                currentSteps[i] = 0;
                playheadPhases[i] = 0.0f;
            }
        } else {
            playMode = !playMode;
        }
    }
    if (btnTriggers[5].process(params[PROB_PARAM].getValue())) probMode = !probMode;
    if (btnTriggers[4].process(params[CHRD_PARAM].getValue())) chordMode = !chordMode;
    if (btnTriggers[2].process(params[ARP_PARAM].getValue())) arpMode = !arpMode;
    if (btnTriggers[3].process(params[FRZ_PARAM].getValue())) frzMode = !frzMode;

    // 2. Sequencer Step Advancing (Internal Tempo vs Analog External CLK)
    bool hasClock = inputs[CLK_INPUT].isConnected();
    bool stepTriggered = false;

    if (hasClock) {
        if (clockTrigger.process(inputs[CLK_INPUT].getVoltage())) {
            stepTriggered = true;
        }
    }

    if (playMode) {
        for (int chan = 0; chan < 8; chan++) {
            if (hasClock && stepTriggered) {
                currentSteps[chan] = (currentSteps[chan] + 1) % loopLength[chan];
            } else if (!hasClock) {
                // Internal Clock
                float bpm = params[RATE_PARAM].getValue() * 180.0f + 40.0f; // 40 to 220 BPM
                playheadPhases[chan] += (bpm / 60.0f) * args.sampleTime;
                if (playheadPhases[chan] >= 1.0f) {
                    playheadPhases[chan] -= 1.0f;
                    currentSteps[chan] = (currentSteps[chan] + 1) % loopLength[chan];
                }
            }
        }
    }

    // 3. Capture Physical "Fader / Knob Wiggles" (Malekko Live Automation)
    if (playMode) {
        if (focusedChannel == -1) {
            // Global Fader Routing
            for (int i = 0; i < 8; i++) {
                float faderVal = params[FADER_1_PARAM + i].getValue();
                if (focusedFX != -1) {
                    channelFxSends[focusedFX][i] = faderVal;
                } else if (shftMode) {
                    // Global Shift: Scale overall channel volumes
                    channelAmplitudes[i] = faderVal;
                } else {
                    channelAmplitudes[i] = faderVal;
                }
            }
        } else {
            // Focused Channel Lockout routing
            for (int i = 0; i < 8; i++) {
                if (i != focusedChannel) continue; // Lockout inactive sliders
                
                float faderVal = params[FADER_1_PARAM + i].getValue();
                int currentActiveStep = currentSteps[focusedChannel];

                if (probMode) {
                    stepProbability[focusedChannel][currentActiveStep] = faderVal;
                } else if (shftMode) {
                    // Turn Dynamics knob while focused to alter loop length
                    float dynamicsVal = params[DYNAMICS_PARAM].getValue();
                    loopLength[focusedChannel] = (int)(dynamicsVal * 15.f) + 1; // 1 to 16 steps
                } else {
                    stepVelocity[focusedChannel][currentActiveStep] = faderVal;
                }
            }
        }
    }
}
