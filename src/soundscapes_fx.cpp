#include "soundscapes.hpp"
#include <cmath>
#include <algorithm>

/**
 * 1. Andrew Simper/Trapezoidal Linear State-Variable Filter (SVF)
 */
struct StereoSVF {
    float ic1eq_L = 0.0f, ic2eq_L = 0.0f;
    float ic1eq_R = 0.0f, ic2eq_R = 0.0f;

    void process(float inputL, float inputR, float cutoffHz, float resonance, float sampleRate, float& outL, float& outR) {
        cutoffHz = math::clamp(cutoffHz, 20.0f, 18000.0f);
        resonance = math::clamp(resonance, 0.01f, 0.99f);
        float q = 1.0f / (2.0f * (1.0f - resonance)); // Q-factor mapping

        float g = std::tan(M_PI * cutoffHz / sampleRate);
        float k = 1.0f / q;
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;

        // Left Channel Filtering
        float v3_L = inputL - ic2eq_L;
        float v1_L = a1 * ic1eq_L + a2 * v3_L;
        float v2_L = ic2eq_L + g * v1_L;
        ic1eq_L = 2.0f * v1_L - ic1eq_L;
        ic2eq_L = 2.0f * v2_L - ic2eq_L;
        outL = v2_L; // Lowpass output

        // Right Channel Filtering
        float v3_R = inputR - ic2eq_R;
        float v1_R = a1 * ic1eq_R + a2 * v3_R;
        float v2_R = ic2eq_R + g * v1_R;
        ic1eq_R = 2.0f * v1_R - ic1eq_R;
        ic2eq_R = 2.0f * v2_R - ic2eq_R;
        outR = v2_R; // Lowpass output
    }
};

static StereoSVF mainFilter;

/**
 * 2. Circular-Buffer Pitch-Shifter (+1 Octave) for Shimmer Reverb Feedback Loop
 */
struct ShimmerPitchShifter {
    float buffer[4096] = {0.0f};
    int writePtr = 0;
    float readPtr1 = 0.0f;
    float readPtr2 = 2048.0f;

    float process(float sample) {
        buffer[writePtr] = sample;

        readPtr1 += 2.0f;
        readPtr2 += 2.0f;

        if (readPtr1 >= 4096.0f) readPtr1 -= 4096.0f;
        if (readPtr2 >= 4096.0f) readPtr2 -= 4096.0f;

        int idx1 = (int)readPtr1;
        float frac1 = readPtr1 - idx1;
        float out1 = (1.0f - frac1) * buffer[idx1] + frac1 * buffer[(idx1 + 1) % 4096];

        int idx2 = (int)readPtr2;
        float frac2 = readPtr2 - idx2;
        float out2 = (1.0f - frac2) * buffer[idx2] + frac2 * buffer[(idx2 + 1) % 4096];

        float fade = std::abs(2048.0f - (float)writePtr) / 2048.0f;
        float shiftedOutput = out1 * fade + out2 * (1.0f - fade);

        writePtr = (writePtr + 1) % 4096;

        return shiftedOutput;
    }
};

static ShimmerPitchShifter shimmerShiftL;
static ShimmerPitchShifter shimmerShiftR;

/**
 * 3. Master Consolidated Process Loop
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
    for (int i = 0; i < 8; i++) {
        static float prevFaderVal[8] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
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
    for (int i = 0; i < 8; i++) {
        // Sum mono outputs as base dry signal (balanced spatially across stereo field)
        float panL = 1.0f - ((float)i / 7.0f);
        float panR = (float)i / 7.0f;

        float drySignal = outputs[CH1_OUTPUT + i].getVoltage() / 5.0f; // Scale back to internal unit scale

        inputBusL += drySignal * panL;
        inputBusR += drySignal * panR;
    }

    // --- PROCESSOR 1: FEEDBACK DELAY ---
    float delaySendVal = params[DELAY_PARAM].getValue();
    float fbackVal = params[SPREAD_PARAM].getValue() * 0.95f; // Max feedback clamp: 95%
    float delayTimeVal = params[RATE_PARAM].getValue();

    // Dynamically calculate delay line tap length (Delay range: 50ms - 1.5s)
    int delaySamps = (int)((0.05f + delayTimeVal * 1.45f) * sampleRate);

    // Write to circular delay buffer
    fxUnit.delayBufferL[fxUnit.delayPtr] = inputBusL + (fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000] * fbackVal);
    fxUnit.delayBufferR[fxUnit.delayPtr] = inputBusR + (fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000] * fbackVal);

    float delayOutL = fxUnit.delayBufferL[(fxUnit.delayPtr - delaySamps + 48000) % 48000];
    float delayOutR = fxUnit.delayBufferR[(fxUnit.delayPtr - delaySamps + 48000) % 48000];

    fxUnit.delayPtr = (fxUnit.delayPtr + 1) % 48000;

    // --- PROCESSOR 2: SHIMMER REVERB TANK ---
    float reverbSendVal = params[REVERB_PARAM].getValue();
    float revDecayVal = params[DYNAMICS_PARAM].getValue() * 0.97f; // Max decay clamp: 97%
    float timbrePitchShiftVal = params[TIMBRE_PARAM].getValue();

    // Reverb loop processing
    float revInputL = inputBusL + delayOutL * delaySendVal;
    float revInputR = inputBusR + delayOutR * delaySendVal;

    // Pitch shift reverb feedback trails for shimmer effect
    float feedbackShiftL = shimmerShiftL.process(fxUnit.reverbState) * timbrePitchShiftVal;
    float feedbackShiftR = shimmerShiftR.process(fxUnit.reverbState) * timbrePitchShiftVal;

    // FDN feedback accumulation
    float revL = revInputL + (fxUnit.reverbState + feedbackShiftL) * revDecayVal;
    float revR = revInputR + (fxUnit.reverbState + feedbackShiftR) * revDecayVal;

    // Store mono damp accumulator
    fxUnit.reverbState = (revL + revR) * 0.5f;

    // --- PROCESSOR 3: STATE-VARIABLE FILTER (SVF) ---
    float filterSendVal = params[FILTER_PARAM].getValue();
    float cutoffVal = params[TEXTURE_PARAM].getValue() * 12000.0f + 100.0f; // Cutoff: 100Hz - 12.1kHz
    float resonanceVal = params[DENSITY_PARAM].getValue() * 0.95f; // Resonance: 0% to 95%

    float filteredOutL = 0.0f;
    float filteredOutR = 0.0f;

    // Run active filter process
    mainFilter.process(revL, revR, cutoffVal, resonanceVal, sampleRate, filteredOutL, filteredOutR);

    // --- FINAL MASTER OUTPUT MIXING & ROUTING ---
    float wetL = (delayOutL * delaySendVal) + (revL * reverbSendVal) + (filteredOutL * filterSendVal);
    float wetR = (delayOutR * delaySendVal) + (revR * reverbSendVal) + (filteredOutR * filterSendVal);

    // Write final stereo master bus outputs
    for (int i = 0; i < 8; i++) {
        if (outputs[CH1_OUTPUT + i].isConnected()) {
            // Read active dry signal
            float drySignal = outputs[CH1_OUTPUT + i].getVoltage();
            
            // Sum dry with wet processed spatial signals relative to stereo output channels
            float panL = 1.0f - ((float)i / 7.0f);
            float panR = (float)i / 7.0f;

            float finalOutL = drySignal + (wetL * panL * 5.0f);
            float finalOutR = drySignal + (wetR * panR * 5.0f);

            // Left output (CH 1–4) / Right output (CH 5–8) separation
            if (i < 4) {
                outputs[CH1_OUTPUT + i].setVoltage(finalOutL);
            } else {
                outputs[CH1_OUTPUT + i].setVoltage(finalOutR);
            }
        }
    }
}
