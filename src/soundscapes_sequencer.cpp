#include "soundscapes.hpp"

// Static trigger helpers for sequencer and interface controls
static dsp::SchmittTrigger clockTrigger;
static dsp::SchmittTrigger resetTrigger;
static dsp::SchmittTrigger playTrigger;

/**
 * Initialise default sequencer sequences
 */
void Soundscapes::initializeSequence() {
    for (int i = 0; i < 8; i++) {
        melodyTrack.steps[i].note = i; // Diatonic step offsets
        melodyTrack.steps[i].velocity = 100;
        melodyTrack.steps[i].probability = 100;
        melodyTrack.steps[i].active = (i % 2 == 0); // Default alternating pattern

        chordTrack.steps[i].note = i + 2;
        chordTrack.steps[i].velocity = 90;
        chordTrack.steps[i].probability = 80;
        chordTrack.steps[i].active = (i % 4 == 0);
    }
}

/**
 * Handle Display Clicks to toggle Channel Focus Locks
 */
void Soundscapes::handleFocusToggle(int channel) {
    if (focusedChannel == channel) {
        focusedChannel = -1;
    } else {
        focusedChannel = channel;
    }
}

/**
 * Handle complex fader mappings based on Global Mixer vs. Focus Mode
 */
void Soundscapes::handleFaderMapping() {
    shiftActive = params[SHFT_PARAM].getValue() > 0.5f;

    if (shiftActive) {
        activeFaderState = FADER_MIXER; 
    } else if (params[FM_PARAM].getValue() > 0.5f) {
        activeFaderState = FADER_FM_SEND;
    } else if (params[DELAY_PARAM].getValue() > 0.5f) {
        activeFaderState = FADER_DELAY_SEND;
    } else if (params[REVERB_PARAM].getValue() > 0.5f) {
        activeFaderState = FADER_REVERB_SEND;
    } else if (params[FILTER_PARAM].getValue() > 0.5f) {
        activeFaderState = FADER_FILTER_SEND;
    } else {
        activeFaderState = FADER_MIXER;
    }

    if (focusedChannel == -1) {
        // --- GLOBAL MIXER MODE ---
        for (int i = 0; i < 8; i++) {
            float faderVal = params[FADER1_PARAM + i].getValue();
            if (activeFaderState == FADER_MIXER) {
                channelVolumes[i] = faderVal;
            } else {
                int fxIndex = (int)activeFaderState - 1;
                fxSends[fxIndex][i] = faderVal;
            }
        }
    } else {
        // --- CHANNEL FOCUS MODE ---
        for (int i = 0; i < 8; i++) {
            if (i != focusedChannel) {
                continue;
            }

            float faderVal = params[FADER1_PARAM + i].getValue();
            int currentStep = melodyTrack.playhead; 

            if (shiftActive) {
                melodyTrack.steps[currentStep].probability = (uint8_t)(faderVal * 100.0f);
                chordTrack.steps[currentStep].probability = (uint8_t)(faderVal * 100.0f);
            } else {
                melodyTrack.steps[currentStep].velocity = (uint8_t)(faderVal * 127.0f);
                chordTrack.steps[currentStep].velocity = (uint8_t)(faderVal * 127.0f);
            }
        }
    }
}

/**
 * Step Sequencer Advance & Timing Loop
 */
void Soundscapes::processSequencer(float sampleTime) {
    // 1. Handle Display Flashing state timer
    flashTimer += sampleTime;
    if (flashTimer >= 0.25f) {
        flashTimer = 0.0f;
        displayFlashState = !displayFlashState;
    }

    // 2. Play/Stop Transport toggle
    if (playTrigger.process(params[PLAY_PARAM].getValue())) {
        if (shiftActive) {
            melodyTrack.playhead = 0;
            chordTrack.playhead = 0;
        } else {
            isPlaying = !isPlaying;
        }
    }

    // 3. Clear/Initialize Sequence toggle
    if (params[CHRD_PARAM].getValue() > 0.5f && shiftActive) {
        if (focusedChannel != -1) {
            for (int i = 0; i < 8; i++) {
                params[STEP_PARAM_START + i].setValue(0.0f);
                params[STEP_PARAM_START + 8 + i].setValue(0.0f);
            }
        }
    }

    // 4. Map active sequencer steps directly to the parameter values to solve the double-click bug
    for (int i = 0; i < 8; i++) {
        melodyTrack.steps[i].active = (params[STEP_PARAM_START + i].getValue() > 0.5f);
        chordTrack.steps[i].active = (params[STEP_PARAM_START + 8 + i].getValue() > 0.5f);
    }

    // 5. Schmitt triggers for external hardware signals
    bool externalClock = clockTrigger.process(inputs[CLK_INPUT].getVoltage());
    bool externalReset = resetTrigger.process(inputs[RST_INPUT].getVoltage());

    if (externalReset) {
        melodyTrack.playhead = 0;
        chordTrack.playhead = 0;
    }

    if (externalClock && isPlaying) {
        melodyTrack.playhead = (melodyTrack.playhead + 1) % 8;
        chordTrack.playhead = (chordTrack.playhead + 1) % 8;

        int melIdx = melodyTrack.playhead;
        int chdIdx = chordTrack.playhead;

        if (melodyTrack.steps[melIdx].active) {
            int roll = (int)(rand() % 100);
            if (roll < melodyTrack.steps[melIdx].probability) {
                voices[melIdx].trigger();
            }
        }

        if (chordTrack.steps[chdIdx].active) {
            int roll = (int)(rand() % 100);
            if (roll < chordTrack.steps[chdIdx].probability) {
                voices[chdIdx].trigger();
            }
        }
    }

    // Update LED step lights for visual tracker
    for (int i = 0; i < 8; i++) {
        bool isMelPlayhead = (melodyTrack.playhead == i) && isPlaying;
        lights[STEP_LED_START + i].setBrightness(isMelPlayhead ? 1.0f : (melodyTrack.steps[i].active ? 0.3f : 0.0f));

        bool isChdPlayhead = (chordTrack.playhead == i) && isPlaying;
        lights[STEP_LED_START + 8 + i].setBrightness(isChdPlayhead ? 1.0f : (chordTrack.steps[i].active ? 0.3f : 0.0f));
    }

    lights[PLAY_LED].setBrightness(isPlaying ? 1.0f : 0.0f);
    lights[SHFT_LED].setBrightness(shiftActive ? 1.0f : 0.0f);
}
