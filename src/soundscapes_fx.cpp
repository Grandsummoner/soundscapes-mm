#include "soundscapes.hpp"
#include <cmath>
#include <algorithm>

/**
 * Master Consolidated Process Loop
 */
void Soundscapes::process(const ProcessArgs& args) {
    // A. Tick Sequencer Step clocks & handle focused fader locks
    processSequencer(args.sampleTime);
    handleFaderMapping();

    // B. Temporary HUD Displays update timers
    for (int i = 0; i < 8; i++) {
        if (displayValueTimer[i] > 0.0f) {
            displayValueTimer[i] -= args.sampleTime;
        }
    }

    // C. Monitor fader movements to display active percentages (00 - 99)
    for (int i = 0; i < 6; i++) {
        static float prevFaderVal[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        float currVal = params[FADER1_PARAM + i].getValue();
        if (prevFaderVal[i] >= 0.0f && fabs(currVal - prevFaderVal[i]) > 0.001f) {
            displayValueTimer[i] = 1.5f;
            displayValue[i] = currVal;
            displayType[i] = 0;
        }
        prevFaderVal[i] = currVal;
    }

    // D. Monitor macro knobs to display edited levels across all screens
    for (int k = 0; k < 6; k++) {
        static float prevKnobVal[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        float currVal = params[RATE_PARAM + k].getValue();
        if (prevKnobVal[k] >= 0.0f && fabs(currVal - prevKnobVal[k]) > 0.001f) {
            for (int i = 0; i < 8; i++) {
                displayValueTimer[i] = 1.5f;
                displayValue[i] = currVal;
                displayType[i] = 0;
            }
        }
        prevKnobVal[k] = currVal;
    }

    // E. Monitor ROOT & SCALE edits to display note names and scale abbreviations
    static float prevRoot = -1.0f;
    float currRoot = params[ROOT_PARAM].getValue();
    if (prevRoot >= 0.0f && fabs(currRoot - prevRoot) > 0.01f) {
        for (int i = 0; i < 8; i++) {
            displayValueTimer[i] = 1.5f;
            displayValue[i] = currRoot;
            displayType[i] = 1;
        }
    }
    prevRoot = currRoot;

    static float prevScale = -1.0f;
    float currScale = params[SCALE_PARAM].getValue();
    if (prevScale >= 0.0f && fabs(currScale - prevScale) > 0.01f) {
        for (int i = 0; i < 8; i++) {
            displayValueTimer[i] = 1.5f;
            displayValue[i] = currScale;
            displayType[i] = 2;
        }
    }
    prevScale = currScale;

    // F. Call Synthesizer Engine to process raw Voice DSP output signals
    processDSP(args);

    // G. Cascade FX Tank Bus Processing
    float sampleRate = args.sampleRate;
    float inputBusL = 0.0f;
    float inputBusR = 0.0f;

    // Accumulate stereo audio signals from active voice channel outputs
    for (int i = 0; i < 6; i++) {
        float panL = 1.0f - ((float)i / 5.0f);
        float panR = (float)i / 5.0f;

        // Retrieve raw synthesized voice signals directly
        float drySignal = outputs[CH1_OUTPUT + i].getVoltage() / 5.0f; 

        inputBusL += drySignal * panL;
        inputBusR += drySignal * panR;
    }

    // --- PROCESSOR 1: FEEDBACK DELAY ---
    float delaySendVal = params[DELAY_PARAM].getValue();
    float fbackVal = params[SPREAD_PARAM].getValue() * 0.95f; // Max feedback clamp: 95%
    float delayTimeVal = params[RATE_PARAM].getValue();

    // Mode-dependent delay character (unannounced -- same DELAY button/knobs,
    // different algorithm underneath): Voices keeps the original ping-pong delay
    // unchanged; Waves is shorter and more resonant, reinforcing the plucked
    // string's own harmonic ring rather than repeating it plainly; Drone & Dust
    // is a long smear that blurs into the texture rather than reading as discrete echoes.
    float delayMinFrac, delayMaxFrac, dampAmount;
    if (activeSynthMode == MODE_WAVES) {
        delayMinFrac = 0.01f; delayMaxFrac = 0.30f; dampAmount = 0.15f; // Short & bright
    } else if (activeSynthMode == MODE_DRONE_DUST) {
        delayMinFrac = 0.15f; delayMaxFrac = 0.95f; dampAmount = 0.55f; // Long & dark smear
    } else {
        delayMinFrac = 0.05f; delayMaxFrac = 0.90f; dampAmount = 0.30f; // Voices: original character
    }
    float delayFrac = expMap(delayTimeVal, delayMinFrac, delayMaxFrac); // Exponential: was linear

    int delaySamps = (int)(delayFrac * sampleRate);
    delaySamps = math::clamp(delaySamps, 10, 47900);

    // Feedback-path HF damping (tape/echo-style darkening on repeats) -- "musical"
    // improvement: repeats now darken over time instead of staying full-bandwidth
    // forever. Mode-dependent amount matches the character above.
    float dampCoeff = 1.0f - dampAmount;
    float fbTapL = fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000];
    float fbTapR = fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000];
    fxUnit.delayDampL += (fbTapL - fxUnit.delayDampL) * dampCoeff;
    fxUnit.delayDampR += (fbTapR - fxUnit.delayDampR) * dampCoeff;

    // Write to circular delay buffer (cross-feeding L/R -- ping-pong)
    float delayWriteL = inputBusL + (fxUnit.delayDampR * fbackVal);
    float delayWriteR = inputBusR + (fxUnit.delayDampL * fbackVal);
    // Defensive: this buffer feeds back into itself every sample, so a single
    // NaN/Inf value would otherwise persist and poison the delay forever.
    fxUnit.delayBufferL[fxUnit.delayPtr] = std::isfinite(delayWriteL) ? delayWriteL : 0.0f;
    fxUnit.delayBufferR[fxUnit.delayPtr] = std::isfinite(delayWriteR) ? delayWriteR : 0.0f;

    float delayOutL = fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000];
    float delayOutR = fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000];

    fxUnit.delayPtr = (fxUnit.delayPtr + 1) % 48000;

    // --- PROCESSOR 2: SHIMMER REVERB TANK ---
    float reverbSendVal = params[REVERB_PARAM].getValue();
    float timbrePitchShiftVal = params[TIMBRE_PARAM].getValue();

    // Mode-dependent reverb character (unannounced): Voices keeps the original
    // shimmer reverb unchanged; Waves is denser/brighter (plate-like, complements
    // Karplus-Strong plucked strings which want more shimmer/ring than a hall);
    // Drone & Dust is a long, heavily-damped hall/wash (spacious, dark, built for
    // sustained texture rather than discrete notes).
    float revDecayCeiling, shimmerCeiling;
    if (activeSynthMode == MODE_WAVES) {
        revDecayCeiling = 0.75f; shimmerCeiling = 0.65f; // Denser/brighter, shorter
    } else if (activeSynthMode == MODE_DRONE_DUST) {
        revDecayCeiling = 0.98f; shimmerCeiling = 0.25f; // Long, dark, heavily damped
    } else {
        revDecayCeiling = 0.95f; shimmerCeiling = 0.45f; // Voices: original character
    }
    float revDecayVal = params[DYNAMICS_PARAM].getValue() * revDecayCeiling;

    // Reverb loop processing
    float revInputL = inputBusL + delayOutL * delaySendVal;
    float revInputR = inputBusR + delayOutR * delaySendVal;

    // Pitch shift reverb feedback trails for shimmer effect
    float feedbackShiftL = shimmerShiftL.process(fxUnit.reverbState);
    float feedbackShiftR = shimmerShiftR.process(fxUnit.reverbState);

    // Corrected: Crossfade shimmer contribution to guarantee feedback path gain remains strictly stable (< 1.0)
    float shimmerAmount = timbrePitchShiftVal * shimmerCeiling; // Max shimmer blend, mode-dependent
    float feedbackSampleL = (fxUnit.reverbState * (1.0f - shimmerAmount) + feedbackShiftL * shimmerAmount) * revDecayVal;
    float feedbackSampleR = (fxUnit.reverbState * (1.0f - shimmerAmount) + feedbackShiftR * shimmerAmount) * revDecayVal;

    float revL = revInputL + feedbackSampleL;
    float revR = revInputR + feedbackSampleR;

    // Store mono damp accumulator
    float newReverbState = (revL + revR) * 0.5f;
    fxUnit.reverbState = std::isfinite(newReverbState) ? newReverbState : 0.0f;

    // --- PROCESSOR 3: STATE-VARIABLE FILTER (SVF) ---
    float filterSendVal = params[FILTER_PARAM].getValue();
    float cutoffVal = expMap(params[TEXTURE_PARAM].getValue(), 80.0f, 12000.0f); // Exponential: was linear 100Hz-12.1kHz
    // Resonance: was a fixed 0.35 constant -- now a real second knob (TIMBRE),
    // safe to share with Reverb's shimmer control since Reverb/Filter/Compressor
    // are mutually exclusive (only one bus active at a time).
    float resonanceVal = 0.1f + params[TIMBRE_PARAM].getValue() * 0.75f; // 0.1-0.85, stays clear of self-oscillation

    float filteredOutL = 0.0f;
    float filteredOutR = 0.0f;

    // Run active filter process
    mainFilter.process(revL, revR, cutoffVal, resonanceVal, sampleRate, filteredOutL, filteredOutR);

    // --- PROCESSOR 4: COMPRESSOR + TILT EQ + MID-SIDE ("Glue") ---
    // One strength knob (TIMBRE) blends compression + tilt brightening together;
    // a separate width knob (TEXTURE) controls mid-side spread independently.
    // Fixed, moderate attack/release times so turning the strength knob reshapes
    // depth, not speed -- deliberately no wild swings in level.
    float compressorSendVal = params[COMPRESSOR_PARAM].getValue();
    float strengthVal = params[TIMBRE_PARAM].getValue();
    float widthVal = params[TEXTURE_PARAM].getValue();

    float rmsIn = std::sqrt((inputBusL * inputBusL + inputBusR * inputBusR) * 0.5f);
    float compAttackCoeff = 1.0f - std::exp(-1.0f / (0.005f * sampleRate));  // ~5ms
    float compReleaseCoeff = 1.0f - std::exp(-1.0f / (0.120f * sampleRate)); // ~120ms
    float envCoeff = (rmsIn > fxUnit.compEnvelope) ? compAttackCoeff : compReleaseCoeff;
    fxUnit.compEnvelope += (rmsIn - fxUnit.compEnvelope) * envCoeff;

    float threshold = 0.6f - strengthVal * 0.45f; // Higher strength -> lower threshold, more gets caught
    float ratio = 1.0f + strengthVal * 3.0f;      // Up to ~4:1, capped -- avoids harsh limiting
    float compGain = 1.0f;
    if (fxUnit.compEnvelope > threshold && fxUnit.compEnvelope > 0.0001f) {
        float over = fxUnit.compEnvelope / threshold;
        compGain = std::pow(over, (1.0f / ratio) - 1.0f);
    }
    float compOutL = inputBusL * compGain;
    float compOutR = inputBusR * compGain;

    // Tilt EQ: simple one-pole low/high split, tilt amount shares the same
    // strength knob as the compressor (higher strength = brighter/more forward).
    float tiltAmount = (strengthVal - 0.5f) * 0.8f; // Modest range, no harsh extremes
    float tiltCoeff = 1.0f - std::exp(-2.0f * (float)M_PI * 400.0f / sampleRate); // ~400Hz crossover
    fxUnit.tiltLowL += (compOutL - fxUnit.tiltLowL) * tiltCoeff;
    fxUnit.tiltLowR += (compOutR - fxUnit.tiltLowR) * tiltCoeff;
    float tiltHighL = compOutL - fxUnit.tiltLowL;
    float tiltHighR = compOutR - fxUnit.tiltLowR;
    float tiltOutL = fxUnit.tiltLowL * (1.0f - tiltAmount) + tiltHighL * (1.0f + tiltAmount);
    float tiltOutR = fxUnit.tiltLowR * (1.0f - tiltAmount) + tiltHighR * (1.0f + tiltAmount);

    // Mid-side width (independent of strength/tilt)
    float mid = (tiltOutL + tiltOutR) * 0.5f;
    float side = (tiltOutL - tiltOutR) * 0.5f;
    float widthAmt = 0.5f + widthVal * 1.5f; // 0.5x (narrow) to 2x (wide)
    side *= widthAmt;
    float compressorOutL = mid + side;
    float compressorOutR = mid - side;

    // --- FINAL MASTER OUTPUT MIXING & ROUTING ---
    float wetL = (delayOutL * delaySendVal) + (revL * reverbSendVal) + (filteredOutL * filterSendVal) + (compressorOutL * compressorSendVal);
    float wetR = (delayOutR * delaySendVal) + (revR * reverbSendVal) + (filteredOutR * filterSendVal) + (compressorOutR * compressorSendVal);

    // Write final stereo master bus outputs
    float masterSumL = 0.0f;
    float masterSumR = 0.0f;

    // FX Return (global fader, col 7): scales how much of the wet FX tank
    // reaches the master bus. Deliberately NOT applied to the per-channel dry
    // outs below -- those stay clean regardless of this fader, so patching a
    // channel elsewhere in the rack always gets a predictable, unprocessed
    // signal, and only the internal Master L/R mix gets colored by FX Return.
    float fxReturnAmt = params[FX_RETURN_PARAM].getValue();

    for (int i = 0; i < 6; i++) {
        // Per-channel jack: pure dry, untouched by wet FX or FX Return. This is
        // what "individual outs are safe to patch out to the rest of the rack"
        // means in practice -- whatever processDSP() wrote here (already
        // including that channel's own fader/amplitude) is the final word; we
        // don't overwrite it with anything wet-mix-dependent below.
        float drySignal = outputs[CH1_OUTPUT + i].getVoltage();

        float panL = 1.0f - ((float)i / 5.0f);
        float panR = (float)i / 5.0f;

        // Master-bus-only blend: dry contribution + wet contribution scaled by
        // FX Return. This value feeds ONLY masterSumL/R -- it is never written
        // back to outputs[CH1_OUTPUT + i], unlike the old behavior.
        float masterContribL = drySignal + (wetL * panL * 5.0f * fxReturnAmt);
        float masterContribR = drySignal + (wetR * panR * 5.0f * fxReturnAmt);
        if (!std::isfinite(masterContribL)) masterContribL = drySignal;
        if (!std::isfinite(masterContribR)) masterContribR = drySignal;

        // Accumulate into the dedicated master sum unconditionally -- Master L/R
        // should work standalone without every individual channel jack also
        // needing to be patched. Divided by 3 (not 6) for headroom: most patches
        // won't have all 6 voices hitting full amplitude simultaneously, so this
        // keeps typical mixes well clear of 10V while still sounding reasonably
        // loud rather than overly conservative.
        masterSumL += masterContribL;
        masterSumR += masterContribR;
    }

    // Master Level (global fader, col 8): final trim on the master bus output.
    // Previously unwired -- pulling this fader to zero did nothing, since
    // nothing downstream ever read its value. Fixed here.
    float masterLevelAmt = params[MASTER_LEVEL_PARAM].getValue();

    if (outputs[MASTER_L_OUTPUT].isConnected()) {
        float masterOutL = (masterSumL / 3.0f) * masterLevelAmt;
        outputs[MASTER_L_OUTPUT].setVoltage(std::isfinite(masterOutL) ? masterOutL : 0.0f);
    }
    if (outputs[MASTER_R_OUTPUT].isConnected()) {
        float masterOutR = (masterSumR / 3.0f) * masterLevelAmt;
        outputs[MASTER_R_OUTPUT].setVoltage(std::isfinite(masterOutR) ? masterOutR : 0.0f);
    }
    lights[MASTER_L_LED].setBrightness(math::clamp(std::fabs(masterSumL) / 15.0f, 0.0f, 1.0f));
    lights[MASTER_R_LED].setBrightness(math::clamp(std::fabs(masterSumR) / 15.0f, 0.0f, 1.0f));
}
