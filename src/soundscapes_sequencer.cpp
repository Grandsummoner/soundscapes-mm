#include "soundscapes.hpp"

// Static trigger helpers for sequencer and interface controls
static dsp::SchmittTrigger playClickTrigger;
static dsp::SchmittTrigger clockTrigger;
static dsp::SchmittTrigger resetTrigger;

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
    isPlaying = params[PLAY_PARAM].getValue() > 0.5f;

    // Mutually Exclusive Radio Button logic for FM, DELAY, REVERB, and FILTER
    static float prevFm = 0.0f, prevDelay = 0.0f, prevReverb = 0.0f, prevFilter = 0.0f;
    float currFm = params[FM_PARAM].getValue();
    float currDelay = params[DELAY_PARAM].getValue();
    float currReverb = params[REVERB_PARAM].getValue();
    float currFilter = params[FILTER_PARAM].getValue();

    // Check which FX button was newly clicked ON, disabling all other three options
    if (currFm > 0.5f && prevFm <= 0.5f) {
        params[DELAY_PARAM].setValue(0.0f);
        params[REVERB_PARAM].setValue(0.0f);
        params[FILTER_PARAM].setValue(0.0f);
        currDelay = currReverb = currFilter = 0.0f;
    } else if (currDelay > 0.5f && prevDelay <= 0.5f) {
        params[FM_PARAM].setValue(0.0f);
        params[REVERB_PARAM].setValue(0.0f);
        params[FILTER_PARAM].setValue(0.0f);
        currFm = currReverb = currFilter = 0.0f;
    } else if (currReverb > 0.5f && prevReverb <= 0.5f) {
        params[FM_PARAM].setValue(0.0f);
        params[DELAY_PARAM].setValue(0.0f);
        params[FILTER_PARAM].setValue(0.0f);
        currFm = currDelay = currFilter = 0.0f;
    } else if (currFilter > 0.5f && prevFilter <= 0.5f) {
        params[FM_PARAM].setValue(0.0f);
        params[DELAY_PARAM].setValue(0.0f);
        params[REVERB_PARAM].setValue(0.0f);
        currFm = currDelay = currReverb = 0.0f;
    }

    prevFm = currFm;
    prevDelay = currDelay;
    prevReverb = currReverb;
    prevFilter = currFilter;

    // Determine active fader routing mode based on the cleared selection
    if (shiftActive) {
        activeFaderState = FADER_MIXER; 
    } else if (currFm > 0.5f) {
        activeFaderState = FADER_FM_SEND;
    } else if (currDelay > 0.5f) {
        activeFaderState = FADER_DELAY_SEND;
    } else if (currReverb > 0.5f) {
        activeFaderState = FADER_REVERB_SEND;
    } else if (currFilter > 0.5f) {
        activeFaderState = FADER_FILTER_SEND;
    } else {
        activeFaderState = FADER_MIXER;
    }

    // NOTE: focused-channel per-step velocity/probability editing via the faders is
    // on hold for now -- steps (16) and channels (6) are decoupled (a step's target
    // channel isn't 1:1 anymore), so "this channel's own step" no longer cleanly
    // exists. Faders always control channel volume for now, focused or not; a
    // proper per-step velocity/probability editing gesture is a separate decision
    // for later.
    for (int i = 0; i < 6; i++) {
        float faderVal = params[FADER1_PARAM + i].getValue();
        if (activeFaderState == FADER_MIXER) {
            channelVolumes[i] = faderVal * faderVal; // Exponential taper: finer low-end control
        } else {
            int fxIndex = (int)activeFaderState - 1;
            fxSends[fxIndex][i] = faderVal;
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

    // 2. Play/Stop Transport / Rewind
    bool playClicked = playClickTrigger.process(params[PLAY_PARAM].getValue());
    if (playClicked) {
        if (shiftActive) {
            melodyTrack.playhead = 0;
            chordTrack.playhead = 0;
            // Retain actual play state on latching toggle
            params[PLAY_PARAM].setValue(isPlaying ? 1.0f : 0.0f);
        } else {
            isPlaying = !isPlaying;
        }
    }

    // 3. CHRD Mode: latching toggle that repurposes the 16 step pads as a radio-select
    // option menu, mutually exclusive with normal step editing so users can't edit the
    // pattern and browse CHRD options at the same time. All 16 slots are currently
    // just reserved framework -- nothing wired to them yet.
    bool chordModeWasActive = chordModeActive;
    chordModeActive = params[CHRD_PARAM].getValue() > 0.5f;

    if (chordModeActive && !chordModeWasActive) {
        // Just entered CHRD mode. Preserve the old SHFT+CHRD "clear focused pattern"
        // gesture as a one-shot action on the same edge that opens CHRD mode.
        if (shiftActive && focusedChannel != -1) {
            for (int i = 0; i < 8; i++) {
                melodyTrack.steps[i].active = false;
                chordTrack.steps[i].active = false;
            }
        }
        // The step pattern itself lives in melodyTrack/chordTrack .active (already
        // captured above/before this frame), so it's safe to repurpose the raw pads
        // as option-select buttons without losing anything.
        for (int i = 0; i < 16; i++) {
            params[STEP_PARAM_START + i].setValue(0.0f);
        }
        if (chordModeOption >= 0) {
            params[STEP_PARAM_START + chordModeOption].setValue(1.0f);
        }
    } else if (!chordModeActive && chordModeWasActive) {
        // Just left CHRD mode: restore the pads to reflect the real (frozen) pattern.
        for (int i = 0; i < 8; i++) {
            params[STEP_PARAM_START + i].setValue(melodyTrack.steps[i].active ? 1.0f : 0.0f);
            params[STEP_PARAM_START + 8 + i].setValue(chordTrack.steps[i].active ? 1.0f : 0.0f);
        }
    }

    if (chordModeActive) {
        // 16-slot radio-exclusive option selector (only one slot lit at a time).
        // Nothing is wired to any slot yet -- reserved for future CHRD-mode features.
        static float prevOptVal[16] = {0.0f};
        int newlyPressed = -1;
        for (int i = 0; i < 16; i++) {
            float cur = params[STEP_PARAM_START + i].getValue();
            if (cur > 0.5f && prevOptVal[i] <= 0.5f) {
                newlyPressed = i;
            }
            prevOptVal[i] = cur;
        }

        if (newlyPressed != -1) {
            for (int i = 0; i < 16; i++) {
                if (i != newlyPressed) params[STEP_PARAM_START + i].setValue(0.0f);
            }
            chordModeOption = newlyPressed;
        } else {
            bool anySelected = false;
            for (int i = 0; i < 16; i++) {
                if (params[STEP_PARAM_START + i].getValue() > 0.5f) anySelected = true;
            }
            if (!anySelected) chordModeOption = -1;
        }
    } else {
        for (int i = 0; i < 8; i++) {
            melodyTrack.steps[i].active = (params[STEP_PARAM_START + i].getValue() > 0.5f);
            chordTrack.steps[i].active = (params[STEP_PARAM_START + 8 + i].getValue() > 0.5f);
        }
    }

    // 5. Standalone Internal Clock Fallback (advances playhead automatically when CLK input is unpatched)
    bool nextStep = false;
    bool externalClockConnected = inputs[CLK_INPUT].isConnected();
    bool externalReset = resetTrigger.process(inputs[RST_INPUT].getVoltage());

    if (externalReset) {
        melodyTrack.playhead = 0;
        chordTrack.playhead = 0;
    }

    if (externalClockConnected) {
        nextStep = clockTrigger.process(inputs[CLK_INPUT].getVoltage());
    } else {
        float rateVal = params[RATE_PARAM].getValue();
        float period = 1.0f / (1.0f + rateVal * 19.0f);

        static float clockAccumulator = 0.0f;
        clockAccumulator += sampleTime;
        if (clockAccumulator >= period) {
            clockAccumulator = 0.0f;
            nextStep = true;
        }
    }

    if (nextStep) {
        stepTimeElapsed = 0.0f;
        float globalDensityScale = 1.0f;
        if (activeSynthMode == MODE_VOICES) {
            float densityVal = params[DENSITY_PARAM].getValue();
            globalDensityScale = 0.2f + densityVal * 1.6f;
        }
        for (int i = 0; i < 8; i++) {
            int melProb = (int)math::clamp(melodyTrack.steps[i].probability * globalDensityScale, 0.0f, 100.0f);
            int chdProb = (int)math::clamp(chordTrack.steps[i].probability * globalDensityScale, 0.0f, 100.0f);
            voiceTriggerActive[i] = ((rand() % 100) < melProb);
            chordTriggerActive[i] = ((rand() % 100) < chdProb);
        }
    } else {
        stepTimeElapsed += sampleTime;
    }

    if (nextStep && isPlaying) {
        melodyTrack.playhead = (melodyTrack.playhead + 1) % 8;
        chordTrack.playhead = (chordTrack.playhead + 1) % 8;
    }

    for (int i = 0; i < 8; i++) {
        bool isMelPlayhead = (melodyTrack.playhead == i) && isPlaying;
        lights[STEP_LED_START + i].setBrightness(isMelPlayhead ? 1.0f : (melodyTrack.steps[i].active ? 0.3f : 0.0f));

        bool isChdPlayhead = (chordTrack.playhead == i) && isPlaying;
        lights[STEP_LED_START + 8 + i].setBrightness(isChdPlayhead ? 1.0f : (chordTrack.steps[i].active ? 0.3f : 0.0f));
    }

    lights[PLAY_LED].setBrightness(isPlaying ? 1.0f : 0.0f);
    lights[SHFT_LED].setBrightness(shiftActive ? 1.0f : 0.0f);
    lights[CHRD_LED].setBrightness(chordModeActive ? 1.0f : 0.0f);
    noteModeActive = params[PROB_PARAM].getValue() > 0.5f;
    lights[PROB_LED].setBrightness(noteModeActive ? 1.0f : 0.0f);
}
