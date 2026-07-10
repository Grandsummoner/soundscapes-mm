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
    cvInput = math::clamp(cvInput, -5.0f, 5.0f);
    float rawNote = cvInput * 12.0f + 60.0f;
    int octave = std::floor(rawNote / 12.0f);
    int noteInOctave = (int)std::floor(rawNote) % 12;

    // Shift relative to selected root note, ensuring positive modulo results
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
    
    activeSynthMode = (SynthMode)clamp((int)std::round(params[MODE_PARAM].getValue()), 0, 2);

    // Read continuous 1V/Oct CV modulation input
    float rootCV = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage() : 0.0f;
    float baseMidiNote = quantizePitch(rootCV, rootNote, scaleIdx);

    // Parse macro parameter control dials
    float rateVal = params[RATE_PARAM].getValue();
    float densityVal = params[DENSITY_PARAM].getValue();
    float timbreVal = params[TIMBRE_PARAM].getValue();
    float textureVal = params[TEXTURE_PARAM].getValue();
    
    // Remapped Envelopes: SPREAD controls Attack, DYNAMICS controls Release/Decay
    float attackVal = params[SPREAD_PARAM].getValue();
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
        
        // Channel 1 acts as the dedicated bass anchor note (transposed down 1 octave)
        if (i == 0) {
            relativeNoteOffset = -12;
        }

        float voiceMidiNote = baseMidiNote + relativeNoteOffset;
        voice.freq = 440.0f * std::pow(2.0f, (voiceMidiNote - 69.0f) / 12.0f);

        // --- ATTACK-RELEASE (AR) ENVELOPE GENERATOR ---
        float attackTime = 0.001f + attackVal * 2.0f;    // Snappy Attack: 1ms to 2s
        float releaseTime = 0.010f + dynamicsVal * 4.0f; // SNappy Release: 10ms to 4s
        
        float attackCoeff = sampleTime / attackTime;
        float releaseCoeff = sampleTime / releaseTime;

        // Calculate gated step duration (gated to stay high for exactly 50% of step length)
        bool voiceGate = false;
        if (isPlaying) {
            float stepPeriod = 1.0f / (1.0f + rateVal * 19.0f);
            float activeGateDuration = stepPeriod * 0.50f; // 50% gate duration
            bool isGateActive = (stepTimeElapsed < activeGateDuration);

            // Melody Voice check
            if (melodyTrack.playhead == i && melodyTrack.steps[i].active && isGateActive && voiceTriggerActive[i]) {
                voiceGate = true;
            }
            // Chord Voice check
            if (chordTrack.playhead == i && chordTrack.steps[i].active && isGateActive && chordTriggerActive[i]) {
                voiceGate = true;
            }
        }

        if (voiceGate) {
            // Rise phase
            voice.env += attackCoeff * (1.05f - voice.env);
        } else {
            // Decay/Release phase
            voice.env += releaseCoeff * (0.0f - voice.env);
        }
        voice.env = math::clamp(voice.env, 0.0f, 1.0f);

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
            delayLength = math::clamp(delayLength, 4, 2047);

            // Snappy trigger excitation pluck on gate onset
            if (voiceGate && voice.env < 0.05f) {
                voice.delayBuffer[voice.writeIdx] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            }

            int readIdx = (voice.writeIdx - delayLength + 2048) & 2047;
            float currentSample = voice.delayBuffer[readIdx];

            int nextIdx = (readIdx + 1) & 2047;
            float nextSample = voice.delayBuffer[nextIdx];
            float dampSample = (currentSample + nextSample) * 0.5f;

            // Decay speed controlled by Dynamics knob (Macro 6)
            float feedbackMult = 0.9f + (dynamicsVal * 0.098f);
            float feedbackSample = dampSample * feedbackMult;

            voice.writeIdx = (voice.writeIdx + 1) & 2047;
            voice.delayBuffer[voice.writeIdx] = feedbackSample;

            channelOutputSignal = currentSample * voice.env * channelVolumes[i] * 0.8f;

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

            channelOutputSignal = (droneCarrier * 0.4f * voice.env + dustImpulse * 0.6f * voice.env) * channelVolumes[i];
        }

        // Apply dynamic CV DUCKING
        if (inputs[DUCK_INPUT].isConnected()) {
            float duckVolts = inputs[DUCK_INPUT].getVoltage();
            float duckAmount = params[DYNAMICS_PARAM].getValue() * (duckVolts / 10.0f);
            
            float duckAtten = 1.0f - math::clamp(duckAmount, 0.0f, 1.0f);
            channelOutputSignal *= duckAtten;
        }

        // 3. Write pure audio straight to the output jacks!
        if (outputs[CH1_OUTPUT + i].isConnected()) {
            outputs[CH1_OUTPUT + i].setVoltage(channelOutputSignal * 5.0f); // 5V Eurorack peak-to-peak audio output
        }

        lights[CH1_LED + i].setBrightness(voice.env);
    }
}
