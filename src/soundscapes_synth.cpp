#include "soundscapes.hpp"
#include <cmath>
#include <cstdlib>

// Diatonic scales: Major, Natural Minor, Pentatonic, Dorian, Phrygian
static const int SCALES[5][12] = {
    {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19}, // Major
    {0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19}, // Minor
    {0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26}, // Pentatonic Major
    {0, 2, 3, 5, 7, 9, 10, 12, 14, 15, 17, 19}, // Dorian
    {0, 1, 3, 5, 7, 8, 10, 12, 13, 15, 17, 19}  // Phrygian
};

/**
 * Diatonic Pitch Quantization Helper
 */
static float quantizePitch(float cvInput, int root, int scaleIdx) {
    // Clamp input to a standard safe Eurorack voltage range
    cvInput = math::clamp(cvInput, -5.0f, 5.0f);
    
    // Convert 1V/Oct to raw MIDI note centered around middle C (60)
    float rawNote = cvInput * 12.0f + 60.0f;
    int octave = std::floor(rawNote / 12.0f);
    int noteInOctave = (int)std::floor(rawNote) % 12;

    // Shift relative to selected root note, adding 24 to guarantee a positive modulo result
    noteInOctave = (noteInOctave - root + 24) % 12;

    // Find the closest active scale degree in selected scale template
    const int* scale = SCALES[scaleIdx];
    int bestDegree = scale[0];
    int minDiff = 99;

    for (int i = 0; i < 7; i++) {
        int diff = std::abs(scale[i] - noteInOctave);
        if (diff < minDiff) {
            minDiff = diff;
            bestDegree = scale[i];
        }
    }

    // Return the absolute quantized MIDI note number
    return (octave * 12) + bestDegree + root;
}

/**
 * Master DSP Audio Synthesis Engine
 */
void Soundscapes::processDSP(const ProcessArgs& args) {
    float sampleRate = args.sampleRate;
    float sampleTime = args.sampleTime;

    // 1. Parse Global Quantizer Parameters
    int rootNote = (int)std::round(params[ROOT_PARAM].getValue() * 11.0f); // 0 (C) - 11 (B)
    int scaleIdx = (int)std::round(params[SCALE_PARAM].getValue() * 4.0f); // 0 - 4
    
    // Maps active mode directly to the discrete 3-way switch selection
    activeSynthMode = (SynthMode)clamp((int)std::round(params[MODE_PARAM].getValue()), 0, 2);

    // Read continuous 1V/Oct CV modulation input
    float rootCV = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage() : 0.0f;
    float baseMidiNote = quantizePitch(rootCV, rootNote, scaleIdx);

    // Parse macro parameter control dials
    float densityVal = params[DENSITY_PARAM].getValue();
    float timbreVal = params[TIMBRE_PARAM].getValue();
    float textureVal = params[TEXTURE_PARAM].getValue();
    float dynamicsVal = params[DYNAMICS_PARAM].getValue();

    // 2. Synthesize 8 Independent Channels
    for (int i = 0; i < 8; i++) {
        VoiceDSP& voice = voices[i];
        float channelOutputSignal = 0.0f;

        // --- DIATONIC CONSTELLATION MAPPING ---
        int chordOffsetDegree = i * 2; // Harmonizes in thirds
        int chordOctaveOffset = chordOffsetDegree / 7;
        int scaleDegreeIndex = chordOffsetDegree % 7;

        int relativeNoteOffset = SCALES[scaleIdx][scaleDegreeIndex] + (chordOctaveOffset * 12);
        float voiceMidiNote = baseMidiNote + relativeNoteOffset;
        
        voice.freq = 440.0f * std::pow(2.0f, (voiceMidiNote - 69.0f) / 12.0f);

        // Render Envelopes
        float decaySpeed = 1.0f / (0.01f + dynamicsVal * 3.0f);
        voice.env -= decaySpeed * sampleTime;
        if (voice.env < 0.0f) voice.env = 0.0f;

        // --- SYNTHESIS MODEL ROUTER ---
        if (activeSynthMode == MODE_VOICES) {
            float modulatorFreq = voice.freq * (1.0f + std::round(timbreVal * 4.0f));
            float modIndex = textureVal * 4.0f * voice.env;

            voice.phase += modulatorFreq * sampleTime;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;
            float modSignal = std::sin(voice.phase * 2.0f * M_PI) * modIndex;

            float carrierFreq = voice.freq + (modSignal * voice.freq);
            float carrierPhaseStep = carrierFreq * sampleTime;
            voice.phase += carrierPhaseStep;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;

            float rawOscSignal = std::sin(voice.phase * 2.0f * M_PI);

            // First-order LPG Filter Emulation
            float lpgCutoff = voice.env * 3000.0f + 100.0f;
            float alpha = (lpgCutoff * 2.0f * M_PI * sampleTime) / (1.0f + lpgCutoff * 2.0f * M_PI * sampleTime);
            
            voice.noiseState += alpha * (rawOscSignal - voice.noiseState);
            channelOutputSignal = voice.noiseState * voice.env * channelVolumes[i];

        } else if (activeSynthMode == MODE_WAVES) {
            int delayLength = (int)(sampleRate / voice.freq);
            delayLength = math::clamp(delayLength, 4, 2047); // Protect waveguide limits

            if (voice.env > 0.98f) {
                voice.delayBuffer[voice.writeIdx] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            }

            // Safe Bitwise AND index masking (prevents negative modulo memory reading)
            int readIdx = (voice.writeIdx - delayLength + 2048) & 2047;
            float currentSample = voice.delayBuffer[readIdx];

            // Corrected: Uses bitwise mask & 2047 (prevents out-of-bounds index 2048)
            int nextIdx = (readIdx + 1) & 2047;
            float nextSample = voice.delayBuffer[nextIdx];
            float dampSample = (currentSample + nextSample) * 0.5f;

            float feedbackMult = 0.9f + (dynamicsVal * 0.098f);
            float feedbackSample = dampSample * feedbackMult;

            voice.writeIdx = (voice.writeIdx + 1) & 2047;
            voice.delayBuffer[voice.writeIdx] = feedbackSample;

            channelOutputSignal = currentSample * channelVolumes[i] * 0.8f;

        } else if (activeSynthMode == MODE_DRONE_DUST) {
            float phaseShiftOffset = (float)i * 0.125f;
            voice.phase += (voice.freq * 0.5f) * sampleTime + phaseShiftOffset;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;
            float droneCarrier = std::sin(voice.phase * 2.0f * M_PI);

            float dustRateThreshold = densityVal * 0.05f;
            float dustImpulse = 0.0f;
            if (((float)rand() / RAND_MAX) < dustRateThreshold) {
                dustImpulse = ((float)rand() / RAND_MAX - 0.5f) * 1.5f;
            }

            channelOutputSignal = (droneCarrier * 0.4f + dustImpulse * 0.6f) * channelVolumes[i];
        }

        // Apply dynamic CV DUCKING
        if (inputs[DUCK_INPUT].isConnected()) {
            float duckVolts = inputs[DUCK_INPUT].getVoltage();
            float duckAmount = params[DYNAMICS_PARAM].getValue() * (duckVolts / 10.0f);
            
            float duckAtten = 1.0f - math::clamp(duckAmount, 0.0f, 1.0f);
            channelOutputSignal *= duckAtten;
        }

        // Write individual output jacks
        if (outputs[CH1_OUTPUT + i].isConnected()) {
            outputs[CH1_OUTPUT + i].setVoltage(channelOutputSignal * 5.0f);
        }

        lights[CH1_LED + i].setBrightness(voice.env);
    }
}
