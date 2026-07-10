#include "soundscapes.hpp"

// Static trigger helpers for sequencer and interface controls
static dsp::SchmittTrigger clockTrigger;
static dsp::SchmittTrigger resetTrigger;
static dsp::SchmittTrigger playTrigger;
static dsp::SchmittTrigger stepTriggers[16];

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
        // Clicking focused channel display again exits focus to global mixer
        focusedChannel = -1;
    } else {
        // Toggle on channel focus
        focusedChannel = channel;
    }
}

/**
 * Handle complex fader mappings based on Global Mixer vs. Focus Mode
 */
void Soundscapes::handleFaderMapping() {
    // 1. Check Shift state
    shiftActive = params[SHFT_PARAM].getValue() > 0.5f;

    // Shift overrides and disables FX selection to prevent mapping conflicts
    if (shiftActive) {
        activeFaderState = FADER_MIXER; // Bypasses active FX send edit mode
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

    // 2. Map Faders based on Focus Mode
    if (focusedChannel == -1) {
        // --- GLOBAL MIXER MODE ---
        for (int i = 0; i < 8; i++) {
            float faderVal = params[FADER1_PARAM + i].getValue();
            if (activeFaderState == FADER_MIXER) {
                // Controls global amplitude/volume
                channelVolumes[i] = faderVal;
            } else {
                // Controls specific send depth to active FX processors
                int fxIndex = (int)activeFaderState - 1;
                fxSends[fxIndex][i] = faderVal;
            }
        }
    } else {
        // --- CHANNEL FOCUS MODE ---
        // Lock out all faders except the active focused channel's fader
        for (int i = 0; i < 8; i++) {
            if (i != focusedChannel) {
                // Freeze/ignore edits on non-focused channel parameters
                continue;
            }

            float faderVal = params[FADER1_PARAM + i].getValue();
            int currentStep = melodyTrack.playhead; // Edits parameter of active playhead step

            if (shiftActive) {
                // Shift ON: Fader edits step probability weight (0% to 100%)
                melodyTrack.steps[currentStep].probability = (uint8_t)(faderVal * 100.0f);
                chordTrack.steps[currentStep].probability = (uint8_t)(faderVal * 100.0f);
            } else {
                // Shift OFF: Fader edits step velocity (0 to 127)
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
            // SHFT + PLAY: Manual Rewind/Reset to Step 1
            melodyTrack.playhead = 0;
            chordTrack.playhead = 0;
        } else {
            isPlaying = !isPlaying;
        }
    }

    // 3. Clear/Initialize Sequence toggle
    if (params[CHRD_PARAM].getValue() > 0.5f && shiftActive) {
        // SHFT + CHRD: Clear active focused channel's sequence
        if (focusedChannel != -1) {
            for (int i = 0; i < 8; i++) {
                melodyTrack.steps[i].active = false;
                chordTrack.steps[i].active = false;
            }
        }
    }

    // 4. Toggle Step states via step pad clicks
    for (int i = 0; i < 16; i++) {
        if (stepTriggers[i].process(params[STEP_PARAM_START + i].getValue())) {
            if (i < 8) {
                melodyTrack.steps[i].active = !melodyTrack.steps[i].active;
            } else {
                chordTrack.steps[i - 8].active = !chordTrack.steps[i - 8].active;
            }
        }
    }

    // 5. Schmitt triggers for external hardware signals
    bool externalClock = clockTrigger.process(inputs[CLK_INPUT].getVoltage());
    bool externalReset = resetTrigger.process(inputs[RST_INPUT].getVoltage());

    if (externalReset) {
        melodyTrack.playhead = 0;
        chordTrack.playhead = 0;
    }

    // Advance sequencer clock
    if (externalClock && isPlaying) {
        // Advance playheads
        melodyTrack.playhead = (melodyTrack.playhead + 1) % 8;
        chordTrack.playhead = (chordTrack.playhead + 1) % 8;

        // Process step output triggers
        int melIdx = melodyTrack.playhead;
        int chdIdx = chordTrack.playhead;

        // Process Melody step trigger
        if (melodyTrack.steps[melIdx].active) {
            // Evaluate trigger probability
            int roll = (int)(rand() % 100);
            if (roll < melodyTrack.steps[melIdx].probability) {
                // Trigger Voice corresponding to active channel
                voices[melIdx].trigger();
            }
        }

        // Process Chord step trigger
        if (chordTrack.steps[chdIdx].active) {
            int roll = (int)(rand() % 100);
            if (roll < chordTrack.steps[chdIdx].probability) {
                // Trigger offset chord voices
                voices[chdIdx].trigger();
            }
        }
    }

    // Update LED step lights for visual tracker
    for (int i = 0; i < 8; i++) {
        // Melody steps active lights
        bool isMelPlayhead = (melodyTrack.playhead == i) && isPlaying;
        lights[STEP_LED_START + i].setBrightness(isMelPlayhead ? 1.0f : (melodyTrack.steps[i].active ? 0.3f : 0.0f));

        // Chord steps active lights
        bool isChdPlayhead = (chordTrack.playhead == i) && isPlaying;
        lights[STEP_LED_START + 8 + i].setBrightness(isChdPlayhead ? 1.0f : (chordTrack.steps[i].active ? 0.3f : 0.0f));
    }

    // Update transport play button light
    lights[PLAY_LED].setBrightness(isPlaying ? 1.0f : 0.0f);
    lights[SHFT_LED].setBrightness(shiftActive ? 1.0f : 0.0f);
}
