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
/**
 * Crossfader Scene A/B: capture-on-press, then continuous interpolation of every
 * captured param once both scenes exist. Writing directly into params[...] here
 * is what makes the fader/knob widgets visibly move as the crossfader slides --
 * they all just read getValue() to draw themselves, same as a manual drag would.
 */
void Soundscapes::handleSceneMorph() {
    float currSceneA = params[SCENE_A_PARAM].getValue();
    float currSceneB = params[SCENE_B_PARAM].getValue();

    bool sceneAPressed = (currSceneA > 0.5f && prevSceneA <= 0.5f);
    bool sceneBPressed = (currSceneB > 0.5f && prevSceneB <= 0.5f);

    if (sceneAPressed) {
        for (int i = 0; i < 6; i++) sceneA.fader[i] = params[FADER1_PARAM + i].getValue();
        sceneA.fxReturn = params[FX_RETURN_PARAM].getValue();
        sceneA.masterLevel = params[MASTER_LEVEL_PARAM].getValue();
        for (int i = 0; i < 6; i++) sceneA.macro[i] = params[RATE_PARAM + i].getValue();
        sceneA.root = params[ROOT_PARAM].getValue();
        sceneA.scaleParam = params[SCALE_PARAM].getValue();
        sceneA.captured = true;
    }
    if (sceneBPressed) {
        for (int i = 0; i < 6; i++) sceneB.fader[i] = params[FADER1_PARAM + i].getValue();
        sceneB.fxReturn = params[FX_RETURN_PARAM].getValue();
        sceneB.masterLevel = params[MASTER_LEVEL_PARAM].getValue();
        for (int i = 0; i < 6; i++) sceneB.macro[i] = params[RATE_PARAM + i].getValue();
        sceneB.root = params[ROOT_PARAM].getValue();
        sceneB.scaleParam = params[SCALE_PARAM].getValue();
        sceneB.captured = true;
    }

    prevSceneA = currSceneA;
    prevSceneB = currSceneB;

    if (sceneA.captured && sceneB.captured) {
        float pos = params[CROSSFADER_PARAM].getValue();
        for (int i = 0; i < 6; i++) {
            params[FADER1_PARAM + i].setValue(sceneA.fader[i] + (sceneB.fader[i] - sceneA.fader[i]) * pos);
        }
        params[FX_RETURN_PARAM].setValue(sceneA.fxReturn + (sceneB.fxReturn - sceneA.fxReturn) * pos);
        params[MASTER_LEVEL_PARAM].setValue(sceneA.masterLevel + (sceneB.masterLevel - sceneA.masterLevel) * pos);
        for (int i = 0; i < 6; i++) {
            params[RATE_PARAM + i].setValue(sceneA.macro[i] + (sceneB.macro[i] - sceneA.macro[i]) * pos);
        }
        params[ROOT_PARAM].setValue(sceneA.root + (sceneB.root - sceneA.root) * pos);
        params[SCALE_PARAM].setValue(sceneA.scaleParam + (sceneB.scaleParam - sceneA.scaleParam) * pos);
    }
}

void Soundscapes::handleFaderMapping() {
    // --- PITCH/PROB/SAVE/RCL exclusivity ---
    // PITCH defaults to armed and stays that way as the resting state -- editing
    // pitch is the immediate, most common action, so it shouldn't require a
    // button press first. Pressing PROB temporarily takes over (forces PITCH
    // off); when PROB is unlatched again (by the user, not by SAVE stealing it),
    // PITCH automatically resumes. SAVE is fully exclusive: engaging it forces
    // PITCH, PROB, and RCL off. RCL is exclusive with SAVE only: engaging it
    // forces SAVE off, but PITCH/PROB can still be armed alongside it (so a
    // loaded pattern can be tweaked live without leaving RCL).
    float currPitch = params[PITCH_PARAM].getValue();
    float currProb = params[PROB_PARAM].getValue();
    float currSave = params[SAVE_PARAM].getValue();
    float currRcl = params[RCL_PARAM].getValue();

    bool pitchPressed = (currPitch > 0.5f && prevPitch <= 0.5f);
    bool probPressed = (currProb > 0.5f && prevProb <= 0.5f);
    bool probReleased = (currProb <= 0.5f && prevProb > 0.5f); // User-driven unlatch, read
                                                                // before SAVE/RCL can force
                                                                // PROB off below, so a SAVE
                                                                // press doesn't also look like
                                                                // a genuine PROB release here
    bool savePressed = (currSave > 0.5f && prevSave <= 0.5f);
    bool rclPressed = (currRcl > 0.5f && prevRcl <= 0.5f);

    // PITCH <-> PROB: mutually exclusive with each other, with PROB-unlatch
    // auto-restoring PITCH.
    if (pitchPressed) {
        params[PROB_PARAM].setValue(0.0f);
        currProb = 0.0f;
    }
    if (probPressed) {
        params[PITCH_PARAM].setValue(0.0f);
        currPitch = 0.0f;
    } else if (probReleased) {
        params[PITCH_PARAM].setValue(1.0f);
        currPitch = 1.0f;
    }

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

    // --- Mutually Exclusive Radio Button logic for COMPRESSOR, DELAY, REVERB, and FILTER ---
    float currComp = params[COMPRESSOR_PARAM].getValue();
    float currDelay = params[DELAY_PARAM].getValue();
    float currReverb = params[REVERB_PARAM].getValue();
    float currFilter = params[FILTER_PARAM].getValue();

    if (currComp > 0.5f && prevComp <= 0.5f) {
        params[DELAY_PARAM].setValue(0.0f);
        params[REVERB_PARAM].setValue(0.0f);
        params[FILTER_PARAM].setValue(0.0f);
        currDelay = currReverb = currFilter = 0.0f;
    } else if (currDelay > 0.5f && prevDelay <= 0.5f) {
        params[COMPRESSOR_PARAM].setValue(0.0f);
        params[REVERB_PARAM].setValue(0.0f);
        params[FILTER_PARAM].setValue(0.0f);
        currComp = currReverb = currFilter = 0.0f;
    } else if (currReverb > 0.5f && prevReverb <= 0.5f) {
        params[COMPRESSOR_PARAM].setValue(0.0f);
        params[DELAY_PARAM].setValue(0.0f);
        params[FILTER_PARAM].setValue(0.0f);
        currComp = currDelay = currFilter = 0.0f;
    } else if (currFilter > 0.5f && prevFilter <= 0.5f) {
        params[COMPRESSOR_PARAM].setValue(0.0f);
        params[DELAY_PARAM].setValue(0.0f);
        params[REVERB_PARAM].setValue(0.0f);
        currComp = currDelay = currReverb = 0.0f;
    }

    prevComp = currComp;
    prevDelay = currDelay;
    prevReverb = currReverb;
    prevFilter = currFilter;

    if (currComp > 0.5f) {
        activeFaderState = FADER_FM_SEND; // Enum name kept for now (see hpp) -- represents Compressor bus
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

        bool faderMoved = (lastFaderRideValue[i] >= 0.0f) && (fabs(faderVal - lastFaderRideValue[i]) > 0.0005f);
        lastFaderRideValue[i] = faderVal;

        if (pitchArmed || probArmed) {
            // Only actually record when the fader is genuinely being moved --
            // previously this wrote every frame regardless, which meant an
            // untouched fader (sitting at its default resting position) would
            // silently overwrite every step the playhead ever passed with that
            // same value, erasing any melodic variation across the pattern.
            if (faderMoved) {
                float shapedVal = faderVal * faderVal; // Exponential taper, same as the amplitude case below
                if (pitchArmed) stepPitch[i][currentStep] = shapedVal;
                if (probArmed) stepProb[i][currentStep] = shapedVal;
            }
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

    // RATE turned all the way down = stop. The previous formula (1/(1+rateVal*19))
    // only ever reached a 1-second period at rateVal=0 -- never actually stopped,
    // despite the comment above claiming it approached infinity. Below this
    // threshold, freeze the playhead outright instead of just running very slowly.
    bool clockStopped = !externalClockConnected && (params[RATE_PARAM].getValue() < 0.02f);

    if (externalClockConnected) {
        nextStep = clockTrigger.process(inputs[CLK_INPUT].getVoltage());
    } else if (!clockStopped) {
        float rateVal = params[RATE_PARAM].getValue();
        float period = 1.0f / (1.0f + rateVal * 19.0f);

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

        // --- X-Y Wildcard Transpose (live performance pad) ---
        // reachAmt: 0 at pad center, 1 at either edge -- how likely and how big a
        // leap is. yBias: -1..1, which voice group gets more of the movement.
        float xVal = params[WILDCARD_X_PARAM].getValue();
        float yVal = params[WILDCARD_Y_PARAM].getValue();
        float reachAmt = std::fabs(xVal - 0.5f) * 2.0f;

        for (int ch = 0; ch < 6; ch++) {
            // Channel 0 is treated as the nominal "bass" voice for this feature --
            // a simplification, since channels no longer have fixed harmonic roles;
            // if you record your lowest part elsewhere, this balance won't track it.
            bool isBass = (ch == 0);

            // Reach alone drives whether this channel re-rolls this step -- at full
            // deflection (reachAmt=1), every channel re-rolls every step, so the
            // effect is unmistakable whenever X is pushed toward an extreme. Y no
            // longer gates whether anything happens (that diluted the probability
            // enough that it often didn't audibly trigger) -- it now only biases
            // which channel group gets the more dramatic leap.
            if (((float)rand() / RAND_MAX) < reachAmt) {
                float groupBias = isBass ? yVal : (1.0f - yVal); // 0-1, higher = more extreme for this group
                bool bigLeap = ((float)rand() / RAND_MAX) < (reachAmt * (0.4f + groupBias * 0.6f));
                if (bigLeap) {
                    static const int bigIntervals[6] = {-12, -7, -5, 5, 7, 12};
                    channelWildcardOffset[ch] = (float)bigIntervals[rand() % 6];
                } else {
                    static const int smallIntervals[5] = {-2, -1, 0, 1, 2};
                    channelWildcardOffset[ch] = (float)smallIntervals[rand() % 5];
                }
            }
            // If the roll fails, this channel simply keeps whatever offset (if
            // any) it already had -- offsets persist across steps rather than
            // resetting to 0 every time, so a wildcard move sticks until the next
            // one overwrites it, similar to how a recorded pitch stays put.
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
