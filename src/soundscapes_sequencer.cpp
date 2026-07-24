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
        for (int i = 0; i < 6; i++) sceneA.macro[i] = params[TEMPO_PARAM + i].getValue();
        sceneA.root = params[ROOT_PARAM].getValue();
        sceneA.scaleParam = params[SCALE_PARAM].getValue();
        sceneA.captured = true;
    }
    if (sceneBPressed) {
        for (int i = 0; i < 6; i++) sceneB.fader[i] = params[FADER1_PARAM + i].getValue();
        sceneB.fxReturn = params[FX_RETURN_PARAM].getValue();
        sceneB.masterLevel = params[MASTER_LEVEL_PARAM].getValue();
        for (int i = 0; i < 6; i++) sceneB.macro[i] = params[TEMPO_PARAM + i].getValue();
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
            params[TEMPO_PARAM + i].setValue(sceneA.macro[i] + (sceneB.macro[i] - sceneA.macro[i]) * pos);
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
    bool probPressed  = (currProb  > 0.5f && prevProb  <= 0.5f);
    bool savePressed  = (currSave  > 0.5f && prevSave  <= 0.5f);
    bool rclPressed   = (currRcl   > 0.5f && prevRcl   <= 0.5f);

    // PITCH/PROB: mutually exclusive radio toggle -- one is always on.
    // Pressing the active one has no effect (you can't turn both off).
    // Pressing the inactive one switches mode and forces the other off.
    if (pitchPressed && !pitchArmed) {
        params[PROB_PARAM].setValue(0.0f);
        params[PITCH_PARAM].setValue(1.0f);
        currProb = 0.0f; currPitch = 1.0f;
    }
    if (probPressed && !probArmed) {
        params[PITCH_PARAM].setValue(0.0f);
        params[PROB_PARAM].setValue(1.0f);
        currPitch = 0.0f; currProb = 1.0f;
    }

    // SAVE: exclusive -- forces everything else off while slot-picking.
    // After slot pick, PITCH restores (handled in StepPadWidget::onButton).
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

    prevPitch = currPitch; prevProb = currProb;
    prevSave  = currSave;  prevRcl  = currRcl;

    pitchArmed = currPitch > 0.5f;
    probArmed  = currProb  > 0.5f;
    saveArmed  = currSave  > 0.5f;
    rclArmed   = currRcl   > 0.5f;

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

        // Faders ALWAYS control amplitude -- regardless of PITCH/PROB arm state.
        // This was the core design conflict: PITCH armed was stealing the faders
        // for pitch recording, leaving no way to control amplitude. Now faders
        // are permanently the amplitude control; joystick handles pitch/prob recording.
        if (activeFaderState == FADER_MIXER) {
            channelVolumes[i] = faderVal * faderVal; // Exponential taper
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
    // Real BPM clock: TEMPO_PARAM maps 0-1 to 5-180 BPM exponentially
    // so the lower half of the knob covers the ambient 5-60 range and the
    // upper half covers 60-180. External CLK input overrides completely.
    bool nextStep = false;
    bool externalClockConnected = inputs[CLK_INPUT].isConnected();
    bool externalReset = resetTrigger.process(inputs[RST_INPUT].getVoltage());

    if (externalReset) {
        currentStep = 0;
        currentBar = 0;
        for (int i = 0; i < 16; i++) passCount[i] = 0;
    }

    if (externalClockConnected) {
        nextStep = clockTrigger.process(inputs[CLK_INPUT].getVoltage());
    } else {
        float tempoVal = params[TEMPO_PARAM].getValue();
        // Exponential mapping: 0->5 BPM, 0.5->~30 BPM, 1.0->180 BPM
        float bpm = 5.0f * std::pow(36.0f, tempoVal); // 5 * 36^0 = 5, 5 * 36^1 = 180
        bpm = math::clamp(bpm, 5.0f, 180.0f);
        float stepPeriod = 60.0f / (bpm * 4.0f); // 16th note period

        if (tempoVal < 0.01f) {
            // Fully left = stopped
            clockAccumulator = 0.0f;
        } else {
            clockAccumulator += sampleTime;
            if (clockAccumulator >= stepPeriod) {
                clockAccumulator -= stepPeriod;
                nextStep = true;
            }
        }
    }

    // Process channel focus mask from top-row buttons (multi-select toggles)
    static float prevFocusBtn[6] = {};
    for (int i = 0; i < 6; i++) {
        float curr = params[CH_FOCUS_PARAM_START + i].getValue();
        if (curr > 0.5f && prevFocusBtn[i] <= 0.5f) {
            // Toggle this channel's bit in the mask
            channelFocusMask ^= (1 << i);
            // Update legacy single-channel focus for display compat
            focusedChannel = (channelFocusMask == (1 << i)) ? i : -1;
        }
        prevFocusBtn[i] = curr;
    }

    // REV: reverse playback direction for focused channels (or all if none focused)
    static float prevRev = 0.0f;
    float currRev = params[REV_PARAM].getValue();
    if (currRev > 0.5f && prevRev <= 0.5f) reverseActive = !reverseActive;
    prevRev = currRev;

    // INV: invert firing (fill with inverse pattern) for focused channels
    static float prevInv = 0.0f;
    float currInv = params[INV_PARAM].getValue();
    fillActive = currInv > 0.5f;
    prevInv = currInv;

    // Condition buttons (bottom row): set condition for all focused channels
    static float prevCond[8] = {};
    for (int c = 0; c < 8; c++) {
        float curr = params[COND_PARAM_START + c].getValue();
        if (curr > 0.5f && prevCond[c] <= 0.5f) {
            uint8_t mask = channelFocusMask ? channelFocusMask : 0x3F; // all if none focused
            for (int ch = 0; ch < 6; ch++) {
                if (mask & (1 << ch)) channelCondition[ch] = (uint8_t)c;
            }
        }
        prevCond[c] = curr;
    }

    if (nextStep) {
        stepTimeElapsed = 0.0f;

        // Advance playhead (respects REV direction)
        if (reverseActive) {
            currentStep = (currentStep + 15) % 16; // step back
        } else {
            currentStep = (currentStep + 1) % 16;
        }

        // Bar counter: increment every 16 steps
        if (currentStep == 0) {
            currentBar++;
            for (int i = 0; i < 16; i++) passCount[i] = 0;
        }
        passCount[currentStep]++;

        // Joystick Y=pitch, X=prob: write into current step for all channels
        float joystickX = params[WILDCARD_X_PARAM].getValue();
        float joystickY = params[WILDCARD_Y_PARAM].getValue();
        for (int ch = 0; ch < 6; ch++) {
            stepPitch[ch][currentStep] = joystickY;
            stepProb[ch][currentStep]  = joystickX;
        }

        // Per-channel probability roll respecting conditions, density, and INV
        float globalDensityScale = 0.2f + params[DENSITY_PARAM].getValue() * 1.6f;
        for (int ch = 0; ch < 6; ch++) {
            float prob = math::clamp(stepProb[ch][currentStep] * globalDensityScale, 0.0f, 1.0f);
            bool wouldFire = ((float)rand() / RAND_MAX) < prob;

            // Playback condition for this channel
            bool condPass = true;
            switch (channelCondition[ch]) {
                case 1: condPass = (currentBar % 2 == 0); break;           // 1:2
                case 2: condPass = (currentBar % 2 == 1); break;           // 2:2
                case 3: condPass = (currentBar % 4 == 0); break;           // 1:4
                case 4: condPass = (currentBar % 4 == 1); break;           // 2:4
                case 5: condPass = (currentBar % 4 == 2); break;           // 3:4
                case 6: condPass = (currentBar % 8 == 0); break;           // RARE
                case 7: condPass = ((float)rand()/RAND_MAX) > 0.5f; break; // RND
                default: condPass = true; break;                            // ALL
            }

            bool fires = wouldFire && condPass;

            // INV: invert firing for focused channels
            bool chFocused = channelFocusMask ? (channelFocusMask & (1 << ch)) != 0 : true;
            if (fillActive && chFocused) fires = !fires;

            channelTriggerActive[ch] = fires;
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
