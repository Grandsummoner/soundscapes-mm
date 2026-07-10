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
    // Convert 1V/Oct to raw MIDI note centered around middle C (60)
    float rawNote = cvInput * 12.0f + 60.0f;
    int octave = std::floor(rawNote / 12.0f);
    int noteInOctave = (int)std::floor(rawNote) % 12;

    // Shift relative to selected root note
    noteInOctave = (noteInOctave - root + 12) % 12;

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
    
    // Check active synthesis mode selection
    if (params[MODE_PARAM].getValue() > 0.5f) {
        // Simple step cycle through modes
        activeSynthMode = (SynthMode)((activeFaderState + 1) % 3);
    }

    // Read continuous 1V/Oct CV modulation input
    float rootCV = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage() : 0.0f;
    float baseMidiNote = quantizePitch(rootCV, rootNote, scaleIdx);

    // Parse macro parameter control dials
    // float rateVal = params[RATE_PARAM].getValue(); // Commented out to clear unused variable warning
    float densityVal = params[DENSITY_PARAM].getValue();
    float timbreVal = params[TIMBRE_PARAM].getValue();
    float textureVal = params[TEXTURE_PARAM].getValue();
    // float spreadVal = params[SPREAD_PARAM].getValue(); // Commented out to clear unused variable warning
    float dynamicsVal = params[DYNAMICS_PARAM].getValue();

    // 2. Synthesize 8 Independent Channels
    for (int i = 0; i < 8; i++) {
        VoiceDSP& voice = voices[i];
        float channelOutputSignal = 0.0f;

        // --- DIATONIC CONSTELLATION MAPPING ---
        // Clamp Channels 2-8 to diatonic chord structures relative to Channel 1 (root)
        int chordOffsetDegree = i * 2; // Harmonizes in thirds: Root, 3rd, 5th, 7th...
        int chordOctaveOffset = chordOffsetDegree / 7;
        int scaleDegreeIndex = chordOffsetDegree % 7;

        int relativeNoteOffset = SCALES[scaleIdx][scaleDegreeIndex] + (chordOctaveOffset * 12);
        float voiceMidiNote = baseMidiNote + relativeNoteOffset;
        
        // Convert quantized MIDI note to frequency (Hz)
        voice.freq = 440.0f * std::pow(2.0f, (voiceMidiNote - 69.0f) / 12.0f);

        // Render Active Envelopes with Dynamic Decay (Dynamics Dial)
        float decaySpeed = 1.0f / (0.01f + dynamicsVal * 3.0f); // Decay range: 10ms - 3s
        voice.env -= decaySpeed * sampleTime;
        if (voice.env < 0.0f) voice.env = 0.0f;

        // --- SYNTHESIS MODEL ROUTER ---
        if (activeSynthMode == MODE_VOICES) {
            // ==========================================
            // V: Subtractive FM with Low-Pass Gates
            // ==========================================
            // Simple FM Pair: Modulator frequency is scaled by TIMBRE
            float modulatorFreq = voice.freq * (1.0f + std::round(timbreVal * 4.0f));
            float modIndex = textureVal * 4.0f * voice.env;

            // Advance Modulator phase
            voice.phase += modulatorFreq * sampleTime;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;
            float modSignal = std::sin(voice.phase * 2.0f * M_PI) * modIndex;

            // Advance Carrier phase with FM offset
            float carrierFreq = voice.freq + (modSignal * voice.freq);
            float carrierPhaseStep = carrierFreq * sampleTime;
            // Accumulate phase dynamically
            voice.phase += carrierPhaseStep;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;

            float rawOscSignal = std::sin(voice.phase * 2.0f * M_PI);

            // First-order Low-Pass Gate Emulation
            float lpgCutoff = voice.env * 3000.0f + 100.0f; // Filter sweep: 100Hz - 3.1kHz
            float alpha = (lpgCutoff * 2.0f * M_PI * sampleTime) / (1.0f + lpgCutoff * 2.0f * M_PI * sampleTime);
            
            // LPG IIR Filter
            voice.noiseState += alpha * (rawOscSignal - voice.noiseState);
            channelOutputSignal = voice.noiseState * voice.env * channelVolumes[i];

        } else if (activeSynthMode == MODE_WAVES) {
            // ==========================================
            // W: Karplus-String Resonators
            // ==========================================
            int delayLength = (int)(sampleRate / voice.freq);
            if (delayLength > 2047) delayLength = 2047;
            if (delayLength < 4) delayLength = 4;

            // Excitation pluck: Inject a short burst of noise if envelope just triggered
            if (voice.env > 0.98f) {
                // Procedural white noise pluck
                voice.delayBuffer[voice.writeIdx] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            }

            // Read the feedback node sample
            int readIdx = (voice.writeIdx - delayLength + 2048) % 2048;
            float currentSample = voice.delayBuffer[readIdx];

            // Average adjacent samples for low-pass feedback damping
            int nextIdx = (readIdx + 1) % 2048;
            float nextSample = voice.delayBuffer[nextIdx];
            float dampSample = (currentSample + nextSample) * 0.5f;

            // Feedback amount controls string sustain decay (Dynamics Dial)
            float feedbackMult = 0.9f + (dynamicsVal * 0.098f); // Sustain range: Short pluck - long decay
            float feedbackSample = dampSample * feedbackMult;

            // Write feedback back into string waveguide line
            voice.delayBuffer[voice.writeIdx] = feedbackSample;
            voice.writeIdx = (voice.writeIdx + 1) % 2048;

            channelOutputSignal = currentSample * channelVolumes[i] * 0.8f;

        } else if (activeSynthMode == MODE_DRONE_DUST) {
            // ==========================================
            // D: Spatial Drone + Granular Dust Particles
            // ==========================================
            // Detuned spatial drone phase accumulation
            float phaseShiftOffset = (float)i * 0.125f; // Spatial phase-shifted subcarrier offsets
            voice.phase += (voice.freq * 0.5f) * sampleTime + phaseShiftOffset;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;
            float droneCarrier = std::sin(voice.phase * 2.0f * M_PI);

            // Granular Dust "crackles" rate determined by Density parameters
            float dustRateThreshold = densityVal * 0.05f; // Generates random clicks
            float dustImpulse = 0.0f;
            if (((float)rand() / RAND_MAX) < dustRateThreshold) {
                dustImpulse = ((float)rand() / RAND_MAX - 0.5f) * 1.5f;
            }

            // Drone pad + granular dust impulses mixed together
            channelOutputSignal = (droneCarrier * 0.4f + dustImpulse * 0.6f) * channelVolumes[i];
        }

        // Apply dynamic CV DUCKING from analog sidechain envelope follower
        if (inputs[DUCK_INPUT].isConnected()) {
            float duckVolts = inputs[DUCK_INPUT].getVoltage();
            float duckAmount = params[DYNAMICS_PARAM].getValue() * (duckVolts / 10.0f);
            
            // Replaced std::clamp with math::clamp for C++11 compiler compatibility
            float duckAtten = 1.0f - math::clamp(duckAmount, 0.0f, 1.0f);
            channelOutputSignal *= duckAtten;
        }

        // 3. Write individual output jacks
        if (outputs[CH1_OUTPUT + i].isConnected()) {
            outputs[CH1_OUTPUT + i].setVoltage(channelOutputSignal * 5.0f); // 5V Eurorack peak-to-peak scale
        }

        // Write channel volume/envelope status to panel LEDs
        lights[CH1_LED + i].setBrightness(voice.env);
    }
}
