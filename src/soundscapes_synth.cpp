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

    // Find the closest active scale degree. Checks the same octave AND the octaves
    // immediately above/below, so a note near the top or bottom of the octave can
    // snap across the boundary to a nearby degree instead of always being pulled
    // toward whatever's closest within 0-11 (which previously biased quantization
    // downward near the octave edge on 4 of the 5 built-in scales).
    const int* scale = SCALES[scaleIdx];
    int bestDegree = scale[0] % 12;
    int minDiff = 999;

    for (int i = 0; i < 7; i++) {
        int degree = scale[i] % 12;
        for (int octShift = -12; octShift <= 12; octShift += 12) {
            int candidate = degree + octShift;
            int diff = std::abs(candidate - noteInOctave);
            if (diff < minDiff) {
                minDiff = diff;
                bestDegree = candidate;
            }
        }
    }

    return (octave * 12) + bestDegree + root;
}

/**
 * PolyBLEP band-limiting correction, used to anti-alias the virtual-analog
 * sawtooth/pulse oscillators in Voices mode (t = phase 0-1, dt = phase increment per sample)
 */
static float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
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
    
    // Remapped Envelopes: DYNAMICS controls Attack, SPREAD controls Release/Decay
    // (swapped from the original mapping -- these names fit the reversed roles better)
    float attackVal = params[DYNAMICS_PARAM].getValue();
    float releaseVal = params[SPREAD_PARAM].getValue();

    // 2. Synthesize 8 Independent Channels
    // If VOCT_INPUT is patched with a polyphonic cable (e.g. from an external keyboard
    // or poly sequencer), each incoming channel drives one physical voice directly --
    // true polyphonic performance. Voices beyond the incoming channel count (or all
    // voices, if the cable is monophonic/unpatched) keep using the internal diatonic
    // harmonizer + step sequencer as before, so live poly playing and generative
    // harmony can coexist across the 8 channels.
    int voctChannels = inputs[VOCT_INPUT].getChannels();
    bool polyPitchActive = voctChannels > 1;

    for (int i = 0; i < 8; i++) {
        VoiceDSP& voice = voices[i];
        float channelOutputSignal = 0.0f;

        bool externalPolyVoice = polyPitchActive && (i < voctChannels);

        float voiceMidiNote;
        if (externalPolyVoice) {
            float noteCV = inputs[VOCT_INPUT].getPolyVoltage(i);
            if (chromaticPassthroughEnabled) {
                // CHRD mode slot 0: bypass the scale quantizer entirely -- useful when
                // an external sequencer/keyboard is already handling pitch/quantization
                // and you want Soundscape to just play what it's sent.
                voiceMidiNote = math::clamp(noteCV, -5.0f, 5.0f) * 12.0f + 60.0f;
            } else {
                // Each incoming poly channel is quantized on its own -- the external
                // source dictates the voicing/chord directly, no internal harmony offset.
                voiceMidiNote = quantizePitch(noteCV, rootNote, scaleIdx);
            }
        } else {
            // --- DIATONIC CONSTELLATION MAPPING (internal generative harmony) ---
            int chordOffsetDegree = i * 2; // Harmonizes in thirds
            int chordOctaveOffset = chordOffsetDegree / 7;
            int scaleDegreeIndex = chordOffsetDegree % 7;

            int relativeNoteOffset = SCALES[scaleIdx][scaleDegreeIndex] + (chordOctaveOffset * 12);

            // Channel 1 acts as the dedicated bass anchor note (transposed down 1 octave)
            if (i == 0) {
                relativeNoteOffset = -12;
            }
            voiceMidiNote = baseMidiNote + relativeNoteOffset;
        }

        voice.freq = 440.0f * std::pow(2.0f, (voiceMidiNote - 69.0f) / 12.0f);

        // --- ATTACK-RELEASE (AR) ENVELOPE GENERATOR ---
        float attackTime = 0.001f + attackVal * 2.0f;    // Snappy Attack: 1ms to 2s
        float releaseTime = 0.010f + releaseVal * 4.0f;  // Snappy Release: 10ms to 4s
        
        float attackCoeff = sampleTime / attackTime;
        float releaseCoeff = sampleTime / releaseTime;

        // Calculate gated step duration (gated to stay high for exactly 50% of step length)
        bool voiceGate = false;
        float triggerVelocityNorm = 1.0f; // Default: full velocity if nothing says otherwise
        if (externalPolyVoice) {
            // True polyphonic performance: gate follows the external GATE cable directly
            // (getPolyVoltage gracefully falls back to the mono gate if GATE_INPUT itself
            // isn't polyphonic, so a single shared gate still works for all poly notes).
            voiceGate = inputs[GATE_INPUT].getPolyVoltage(i) > 1.0f;
            if (inputs[VEL_INPUT].isConnected()) {
                triggerVelocityNorm = math::clamp(inputs[VEL_INPUT].getPolyVoltage(i) / 10.0f, 0.0f, 1.0f);
            }
        } else if (isPlaying) {
            float stepPeriod = 1.0f / (1.0f + rateVal * 19.0f);
            float activeGateDuration = stepPeriod * 0.50f; // 50% gate duration
            bool isGateActive = (stepTimeElapsed < activeGateDuration);

            // Melody Voice check
            if (melodyTrack.playhead == i && melodyTrack.steps[i].active && isGateActive && voiceTriggerActive[i]) {
                voiceGate = true;
                triggerVelocityNorm = melodyTrack.steps[i].velocity / 127.0f;
            }
            // Chord Voice check
            if (chordTrack.playhead == i && chordTrack.steps[i].active && isGateActive && chordTriggerActive[i]) {
                voiceGate = true;
                triggerVelocityNorm = chordTrack.steps[i].velocity / 127.0f;
            }
        }

        if (voiceGate) {
            // Rise phase, scaled by this trigger's velocity so quiet/hard-set steps
            // (or a patched VEL_INPUT CV, in poly mode) actually play quieter/louder.
            voice.env += attackCoeff * (triggerVelocityNorm * 1.05f - voice.env);
        } else {
            // Decay/Release phase
            voice.env += releaseCoeff * (0.0f - voice.env);
        }
        voice.env = math::clamp(voice.env, 0.0f, 1.0f);

        // --- SYNTHESIS MODEL ROUTER ---
        if (activeSynthMode == MODE_VOICES) {
            // "Voices": a virtual-analog voice per channel (band-limited VA oscillator ->
            // resonant filter -> AR envelope). FM/ring-modulation only happens when
            // EXT_INPUT is patched -- with nothing patched, this is a pure, predictable
            // analog voice with no feedback or self-modulation anywhere in the signal path.

            // Independent operator (brightness) envelope: decays faster than the amplitude
            // envelope, used only to shape the EXT IN ring-mod wet amount below.
            float opDecayCoeff = sampleTime / (0.05f + releaseVal * 0.4f);
            if (voiceGate) {
                voice.opEnv += attackCoeff * (1.05f - voice.opEnv);
            } else {
                voice.opEnv += opDecayCoeff * (0.0f - voice.opEnv);
            }
            voice.opEnv = math::clamp(voice.opEnv, 0.0f, 1.0f);

            float rawOscSignal;
            bool extConnected = inputs[EXT_INPUT].isConnected();

            if (extConnected) {
                // --- External audio path: FM-flavored ring-modulation harmonizer ---
                // TIMBRE selects the modulator:carrier ratio from a curated table spanning
                // harmonic (bell/organ-like, low end) through mildly inharmonic (metallic,
                // high end) tones. The modulator is a plain sine with NO self-feedback --
                // predictable and bounded, never harsh or runaway.
                static const float RATIO_TABLE[8] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f};
                int ratioIdx = math::clamp((int)std::round(timbreVal * 7.0f), 0, 7);
                float modulatorFreq = voice.freq * RATIO_TABLE[ratioIdx];

                voice.modPhase += modulatorFreq * sampleTime;
                if (voice.modPhase >= 1.0f) voice.modPhase -= 1.0f;
                float modOut = std::sin(voice.modPhase * 2.0f * M_PI);

                // DENSITY is the dry/processed blend here; opEnv scales how much ring-mod
                // sideband content comes through, so the effect swells with this channel's
                // gate rather than running constantly.
                float extSignal = inputs[EXT_INPUT].getVoltage() / 5.0f;
                float ringModded = extSignal * modOut;
                float wetAmount = densityVal * voice.opEnv;
                rawOscSignal = extSignal * (1.0f - wetAmount) + ringModded * wetAmount;
            } else {
                // --- Internal virtual-analog oscillator path (PolyBLEP band-limited) ---
                float dt = voice.freq / sampleRate;

                voice.phase += dt;
                if (voice.phase >= 1.0f) voice.phase -= 1.0f;

                float saw = 2.0f * voice.phase - 1.0f;
                saw -= polyBlep(voice.phase, dt);

                float pulseWidth = 0.5f;
                float pulsePhase = voice.phase + (1.0f - pulseWidth);
                if (pulsePhase >= 1.0f) pulsePhase -= 1.0f;
                float pulse = (voice.phase < pulseWidth) ? 1.0f : -1.0f;
                pulse += polyBlep(voice.phase, dt);
                pulse -= polyBlep(pulsePhase, dt);

                // TEXTURE morphs the waveshape from sawtooth to pulse -- classic analog
                // waveshape control, giving Voices real tonal range beyond a single timbre.
                float shapeMorph = textureVal;
                float mainOsc = saw * (1.0f - shapeMorph) + pulse * shapeMorph;

                // DENSITY: unison/thickness. Blends in a second copy of the same
                // oscillator, gently detuned (up to +8 cents), for a fuller analog-chorus
                // character. This is a bounded crossfade, not a sum -- turning it up
                // thickens the tone without ever getting louder or unpredictable.
                float detuneRatio = std::pow(2.0f, (densityVal * 8.0f) / 1200.0f);
                float unisonDt = (voice.freq * detuneRatio) / sampleRate;
                voice.unisonPhase += unisonDt;
                if (voice.unisonPhase >= 1.0f) voice.unisonPhase -= 1.0f;

                float unisonSaw = 2.0f * voice.unisonPhase - 1.0f;
                unisonSaw -= polyBlep(voice.unisonPhase, unisonDt);

                float unisonPulsePhase = voice.unisonPhase + (1.0f - pulseWidth);
                if (unisonPulsePhase >= 1.0f) unisonPulsePhase -= 1.0f;
                float unisonPulse = (voice.unisonPhase < pulseWidth) ? 1.0f : -1.0f;
                unisonPulse += polyBlep(voice.unisonPhase, unisonDt);
                unisonPulse -= polyBlep(unisonPulsePhase, unisonDt);

                float unisonOsc = unisonSaw * (1.0f - shapeMorph) + unisonPulse * shapeMorph;

                rawOscSignal = mainOsc * (1.0f - 0.5f * densityVal) + unisonOsc * (0.5f * densityVal);

                // Bass anchor voice (channel 1): blend in a clean sub-octave sine for
                // low-end weight, since it's the fixed root note.
                if (i == 0) {
                    voice.subPhase += (voice.freq * 0.5f) * sampleTime;
                    if (voice.subPhase >= 1.0f) voice.subPhase -= 1.0f;
                    rawOscSignal = rawOscSignal * 0.7f + std::sin(voice.subPhase * 2.0f * M_PI) * 0.3f;
                }
            }

            // Shared resonant lowpass (trapezoidal SVF, envelope-tracked cutoff -- louder
            // notes open the filter, analog-synth style), then gentle saturation for warmth.
            float cutoffHz = math::clamp(150.0f + voice.env * 5000.0f, 50.0f, 15000.0f);
            float g = std::tan(M_PI * cutoffHz / sampleRate);
            float k = 1.2f; // fixed moderate resonance
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;

            float v3 = rawOscSignal - voice.svfBand;
            float v1 = a1 * voice.svfLow + a2 * v3;
            float v2 = voice.svfBand + g * v1;
            voice.svfLow = 2.0f * v1 - voice.svfLow;
            voice.svfBand = 2.0f * v2 - voice.svfBand;

            float saturated = std::tanh(v2 * 1.2f);
            channelOutputSignal = saturated * voice.env * channelVolumes[i];

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

            // Decay speed controlled by the Release macro (SPREAD, Macro 5)
            float feedbackMult = 0.9f + (releaseVal * 0.098f);
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
