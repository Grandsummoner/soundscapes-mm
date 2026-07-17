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

    // Corrected: Clamp delaySamps strictly to stay within 48000 buffer boundaries (prevents negative modulo)
    int delaySamps = (int)((0.05f + delayTimeVal * 0.9f) * sampleRate);
    delaySamps = math::clamp(delaySamps, 10, 47900);

    // Write to circular delay buffer
    float delayWriteL = inputBusL + (fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000] * fbackVal);
    float delayWriteR = inputBusR + (fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000] * fbackVal);
    // Defensive: this buffer feeds back into itself every sample, so a single
    // NaN/Inf value would otherwise persist and poison the delay forever.
    fxUnit.delayBufferL[fxUnit.delayPtr] = std::isfinite(delayWriteL) ? delayWriteL : 0.0f;
    fxUnit.delayBufferR[fxUnit.delayPtr] = std::isfinite(delayWriteR) ? delayWriteR : 0.0f;

    float delayOutL = fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000];
    float delayOutR = fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000];

    fxUnit.delayPtr = (fxUnit.delayPtr + 1) % 48000;

    // --- PROCESSOR 2: SHIMMER REVERB TANK ---
    float reverbSendVal = params[REVERB_PARAM].getValue();
    float revDecayVal = params[DYNAMICS_PARAM].getValue() * 0.95f; // Max decay clamp: 95%
    float timbrePitchShiftVal = params[TIMBRE_PARAM].getValue();

    // Reverb loop processing
    float revInputL = inputBusL + delayOutL * delaySendVal;
    float revInputR = inputBusR + delayOutR * delaySendVal;

    // Pitch shift reverb feedback trails for shimmer effect
    float feedbackShiftL = shimmerShiftL.process(fxUnit.reverbState);
    float feedbackShiftR = shimmerShiftR.process(fxUnit.reverbState);

    // Corrected: Crossfade shimmer contribution to guarantee feedback path gain remains strictly stable (< 1.0)
    float shimmerAmount = timbrePitchShiftVal * 0.45f; // Max shimmer blend
    float feedbackSampleL = (fxUnit.reverbState * (1.0f - shimmerAmount) + feedbackShiftL * shimmerAmount) * revDecayVal;
    float feedbackSampleR = (fxUnit.reverbState * (1.0f - shimmerAmount) + feedbackShiftR * shimmerAmount) * revDecayVal;

    float revL = revInputL + feedbackSampleL;
    float revR = revInputR + feedbackSampleR;

    // Store mono damp accumulator
    float newReverbState = (revL + revR) * 0.5f;
    fxUnit.reverbState = std::isfinite(newReverbState) ? newReverbState : 0.0f;

    // --- PROCESSOR 3: STATE-VARIABLE FILTER (SVF) ---
    float filterSendVal = params[FILTER_PARAM].getValue();
    float cutoffVal = params[TEXTURE_PARAM].getValue() * 12000.0f + 100.0f; // Cutoff: 100Hz - 12.1kHz
    float resonanceVal = 0.35f; // Fixed moderate resonance -- musical, stays clear of self-oscillation

    float filteredOutL = 0.0f;
    float filteredOutR = 0.0f;

    // Run active filter process
    mainFilter.process(revL, revR, cutoffVal, resonanceVal, sampleRate, filteredOutL, filteredOutR);

    // --- FINAL MASTER OUTPUT MIXING & ROUTING ---
    float wetL = (delayOutL * delaySendVal) + (revL * reverbSendVal) + (filteredOutL * filterSendVal);
    float wetR = (delayOutR * delaySendVal) + (revR * reverbSendVal) + (filteredOutR * filterSendVal);

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
