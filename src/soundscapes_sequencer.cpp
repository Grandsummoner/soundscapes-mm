#include "soundscapes.hpp"

// Static trigger helpers for sequencer and interface controls
static dsp::SchmittTrigger clockTrigger;
static dsp::SchmittTrigger resetTrigger;

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
 * Handle complex fader mappings based on Global Mixer vs. Focus Mode, and the
 * PITCH/PROB/SAVE/RCL exclusivity rules for the performance section.
 */
void Soundscapes::handleFaderMapping() {
    // --- PITCH/PROB/SAVE/RCL exclusivity ---
    // SAVE is fully exclusive: engaging it forces PITCH, PROB, and RCL off.
    // RCL is exclusive with SAVE only: engaging it forces SAVE off, but PITCH/PROB
    // can still be armed alongside it (so a loaded pattern can be tweaked live
    // without leaving RCL). PITCH/PROB force SAVE off but can combine with RCL and
    // with each other.
    static float prevPitch = 0.0f, prevProb = 0.0f, prevSave = 0.0f, prevRcl = 0.0f;
    float currPitch = params[PITCH_PARAM].getValue();
    float currProb = params[PROB_PARAM].getValue();
    float currSave = params[SAVE_PARAM].getValue();
    float currRcl = params[RCL_PARAM].getValue();

    bool pitchPressed = (currPitch > 0.5f && prevPitch <= 0.5f);
    bool probPressed = (currProb > 0.5f && prevProb <= 0.5f);
    bool savePressed = (currSave > 0.5f && prevSave <= 0.5f);
    bool rclPressed = (currRcl > 0.5f && prevRcl <= 0.5f);

    if (savePressed) {
        params[PITCH_PARAM].setValue(0.0f);
        params[PROB_PARAM].setValue(0.0f);
        params[RCL_PARAM].setValue(0.0f);
        currPitch = currProb = currRcl = 0.0f;
    }
    if (rclPressed) {
        params[SAVE_PARAM].setValue(0.0f);
        currSave = 0.0f;
    }
    if ((pitchPressed || probPressed) && currSave > 0.5f) {
        params[SAVE_PARAM].setValue(0.0f);
        currSave = 0.0f;
    }

    prevPitch = currPitch;
    prevProb = currProb;
    prevSave = currSave;
    prevRcl = currRcl;

    pitchArmed = currPitch > 0.5f;
    probArmed = currProb > 0.5f;
    saveArmed = currSave > 0.5f;
    rclArmed = currRcl > 0.5f;

    // --- Mutually Exclusive Radio Button logic for FM, DELAY, REVERB, and FILTER ---
    static float prevFm = 0.0f, prevDelay = 0.0f, prevReverb = 0.0f, prevFilter = 0.0f;
    float currFm = params[FM_PARAM].getValue();
    float currDelay = params[DELAY_PARAM].getValue();
    float currReverb = params[REVERB_PARAM].getValue();
    float currFilter = params[FILTER_PARAM].getValue();

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

    if (currFm > 0.5f) {
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

    // --- Channel faders: amplitude by default, live-record when PITCH/PROB armed ---
    // Live-looper style: every sample the fader is actively being moved while
    // armed, it overwrites that channel's stepPitch/stepProb at the CURRENT step
    // again -- so riding it across multiple loop passes keeps refining the pattern.
    for (int i = 0; i < 6; i++) {
        float faderVal = params[FADER1_PARAM + i].getValue();

        if (pitchArmed || probArmed) {
            if (pitchArmed) stepPitch[i][currentStep] = faderVal;
            if (probArmed) stepProb[i][currentStep] = faderVal;
        } else if (activeFaderState == FADER_MIXER) {
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

    // 2. SAVE/RCL slot-picker: while either is armed, the 16 step pads stop being
    // passive playhead indicators and become a memory slot picker instead (see
    // StepPadWidget::onButton for the actual save/load action on press). This
    // block just drives the pads' lit state to show which slots are occupied.
    //
    // 3. Standalone Internal Clock Fallback (advances playhead automatically when
    // CLK input is unpatched). No PLAY button: the module free-runs by default:
    // an external CLK connection takes over advancement, and turning RATE all the
    // way down functions as "stop" (period approaches infinity).
    bool nextStep = false;
    bool externalClockConnected = inputs[CLK_INPUT].isConnected();
    bool externalReset = resetTrigger.process(inputs[RST_INPUT].getVoltage());

    if (externalReset) {
        currentStep = 0;
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
        currentStep = (currentStep + 1) % 16;

        // Roll each channel's probability for the step it's now on. Probability
        // doubles as the on/off flag -- a step at/near 0% is effectively silent,
        // no separate "active" flag needed.
        float globalDensityScale = 1.0f;
        if (activeSynthMode == MODE_VOICES) {
            float densityVal = params[DENSITY_PARAM].getValue();
            globalDensityScale = 0.2f + densityVal * 1.6f;
        }
        for (int ch = 0; ch < 6; ch++) {
            float prob = math::clamp(stepProb[ch][currentStep] * globalDensityScale, 0.0f, 1.0f);
            channelTriggerActive[ch] = ((float)rand() / RAND_MAX) < prob;
        }
    } else {
        stepTimeElapsed += sampleTime;
    }

    // 4. Step pad lights: SAVE/RCL armed -> show which of the 16 memory slots are
    // occupied (dim = empty, bright = occupied). Otherwise -> passive playhead
    // indicator, one step lit brightly as it plays, the rest dark.
    for (int i = 0; i < 16; i++) {
        if (saveArmed || rclArmed) {
            lights[STEP_LED_START + i].setBrightness(slots[i].occupied ? 0.6f : 0.05f);
        } else {
            bool isPlayhead = (currentStep == i);
            lights[STEP_LED_START + i].setBrightness(isPlayhead ? 1.0f : 0.0f);
        }
    }

    lights[PITCH_LED].setBrightness(pitchArmed ? 1.0f : 0.0f);
    lights[PROB_LED].setBrightness(probArmed ? 1.0f : 0.0f);
    lights[SAVE_LED].setBrightness(saveArmed ? 1.0f : 0.0f);
    lights[RCL_LED].setBrightness(rclArmed ? 1.0f : 0.0f);
}
