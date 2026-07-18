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
    float textureVal = params[TEXTURE_PARAM].getValue();
    
    // Remapped Envelopes: DYNAMICS controls Attack, SPREAD controls Release/Decay
    // (swapped from the original mapping -- these names fit the reversed roles better)
    float attackVal = params[DYNAMICS_PARAM].getValue();
    float releaseVal = params[SPREAD_PARAM].getValue();

    // 2. Synthesize 6 Independent Channels + Master L/R Sum

    for (int i = 0; i < 6; i++) {
        VoiceDSP& voice = voices[i];
        float channelOutputSignal = 0.0f;

        // A single mono gate fires every channel together as one chord stab, using
        // whatever pitch the mono V/OCT has transposed the internal harmonizer to --
        // the straightforward keyboard/sequencer use case, standard Eurorack, no poly
        // cable required.
        bool monoGateActive = inputs[GATE_INPUT].isConnected();

        // --- ATTACK-RELEASE (AR) ENVELOPE GENERATOR ---
        float attackTime = expMap(attackVal, 0.001f, 2.0f);    // 1ms to 2s, exponential (was linear --
        float releaseTime = expMap(releaseVal, 0.010f, 4.0f); // center used to default to ~1s/~2s, causing
                                                                // envelopes to overlap and drone; now center
                                                                // lands on a much snappier, musical time)

        float attackCoeff = sampleTime / attackTime;
        float releaseCoeff = sampleTime / releaseTime;

        // --- Determine trigger info FIRST -- this channel's own recorded pitch/
        // probability at the current step, not a shared melody/chord track lookup.
        bool voiceGate = false;
        float triggerVelocityNorm = 1.0f; // No separate per-step velocity anymore --
                                           // probability doubles as the on/off flag,
                                           // amplitude comes from the channel fader.

        if (monoGateActive) {
            // Single mono gate: fire this channel along with every other one, together,
            // as a chord stab -- e.g. a keyboard's GATE OUT into GATE_INPUT plays the
            // whole harmonized chord on each keypress.
            voiceGate = inputs[GATE_INPUT].getVoltage() > 1.0f;
        } else if (!inputs[CLK_INPUT].isConnected() && rateVal < 0.02f) {
            // RATE turned all the way down = stop: gate off unconditionally, so
            // voices release naturally instead of staying stuck sounding on
            // whatever step the playhead was frozen on (see processSequencer).
            voiceGate = false;
        } else {
            float stepPeriod = 1.0f / (1.0f + rateVal * 19.0f);
            float activeGateDuration = stepPeriod * 0.50f; // 50% gate duration
            bool isGateActive = (stepTimeElapsed < activeGateDuration);

            // All 6 channels share one playhead (currentStep), but each has its own
            // probability roll at that step (channelTriggerActive[i], re-rolled every
            // time the playhead advances -- see processSequencer).
            voiceGate = isGateActive && channelTriggerActive[i];
        }

        // --- Pitch computation: this channel's own recorded value at the current
        // step, quantized. Raw 0-1 fader value maps across a 2-octave (14-degree)
        // range so live-recording a pitch has real melodic reach.
        int degreeOffset = (int)std::round(stepPitch[i][currentStep] * 13.0f);
        int octaveOffset = degreeOffset / 7;
        int scaleDegreeIndex = degreeOffset % 7;
        int relativeNoteOffset = SCALES[scaleIdx][scaleDegreeIndex] + (octaveOffset * 12);
        float voiceMidiNote = baseMidiNote + relativeNoteOffset + channelWildcardOffset[i];

        voice.freq = 440.0f * std::pow(2.0f, (voiceMidiNote - 69.0f) / 12.0f);

        if (voiceGate) {
            // Rise phase, scaled by this trigger's velocity so quiet/hard-set steps
            // actually play quieter/louder.
            voice.env += attackCoeff * (triggerVelocityNorm * 1.05f - voice.env);
        } else {
            // Decay/Release phase
            voice.env += releaseCoeff * (0.0f - voice.env);
        }
        voice.env = math::clamp(voice.env, 0.0f, 1.0f);

        // --- SYNTHESIS MODEL ROUTER ---
        if (activeSynthMode == MODE_VOICES) {
            // "Voices": a virtual-analog voice per channel (band-limited VA oscillator ->
            // resonant filter -> AR envelope). Pure, predictable analog voice -- no
            // feedback or self-modulation anywhere in the signal path.

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
            float rawOscSignal = saw * (1.0f - shapeMorph) + pulse * shapeMorph;

            // Bass anchor voice (channel 1): blend in a clean sub-octave sine for
            // low-end weight, since it's the fixed root note.
            if (i == 0) {
                voice.subPhase += (voice.freq * 0.5f) * sampleTime;
                if (voice.subPhase >= 1.0f) voice.subPhase -= 1.0f;
                rawOscSignal = rawOscSignal * 0.7f + std::sin(voice.subPhase * 2.0f * M_PI) * 0.3f;
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

            // One-shot pluck excitation on the gate's rising edge: fill the entire
            // delay line with noise in a single pass, rather than trickle-writing
            // one sample per call while env stays below a threshold (that approach
            // depended on attack being slow enough, and delayLength short enough,
            // for the whole line to get seeded before the envelope rose past the
            // threshold -- low notes with a fast attack often only got partially
            // seeded, giving a weak, thin pluck instead of a full string excitation).
            bool gateRisingEdge = voiceGate && !voice.prevGate;
            if (gateRisingEdge) {
                for (int b = 0; b < 2048; b++) {
                    voice.delayBuffer[b] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
                }
            }
            voice.prevGate = voiceGate;

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

            // DENSITY: string unison/chorus. A second Karplus-Strong voice, gently
            // detuned (up to +8 cents), plucked at the same moment as the main string
            // and crossfaded in -- thickens the tone into a 12-string/chorus character
            // without ever getting louder or less predictable.
            float detuneRatio = std::pow(2.0f, (densityVal * 8.0f) / 1200.0f);
            int unisonDelayLength = math::clamp((int)(sampleRate / (voice.freq * detuneRatio)), 4, 2047);

            if (gateRisingEdge) {
                for (int b = 0; b < 2048; b++) {
                    voice.unisonDelayBuffer[b] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
                }
            }

            int unisonReadIdx = (voice.unisonWriteIdx - unisonDelayLength + 2048) & 2047;
            float unisonCurrentSample = voice.unisonDelayBuffer[unisonReadIdx];
            int unisonNextIdx = (unisonReadIdx + 1) & 2047;
            float unisonNextSample = voice.unisonDelayBuffer[unisonNextIdx];
            float unisonDampSample = (unisonCurrentSample + unisonNextSample) * 0.5f;
            float unisonFeedbackSample = unisonDampSample * feedbackMult;

            voice.unisonWriteIdx = (voice.unisonWriteIdx + 1) & 2047;
            voice.unisonDelayBuffer[voice.unisonWriteIdx] = unisonFeedbackSample;

            float blendedString = currentSample * (1.0f - 0.5f * densityVal) + unisonCurrentSample * (0.5f * densityVal);

            channelOutputSignal = blendedString * voice.env * channelVolumes[i] * 0.8f;

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

            channelOutputSignal = (droneCarrier * 0.75f * voice.env + dustImpulse * 0.55f * voice.env) * channelVolumes[i];
            // Was 0.4/0.6 -- measurably quieter than Voices/Waves (which run near full
            // amplitude), since the drone alone only reached 0.4 peak and dust impulses
            // are sparse. Boosted for comparable loudness across all three modes.
        }

        // Octatrack-style crossfader: equal-power morph between channels 1-3 and
        // 4-6, so sliding from one end to the other doesn't cause an overall
        // level dip at center.
        float crossfaderPos = params[CROSSFADER_PARAM].getValue();
        float groupAWeight = std::cos(crossfaderPos * (float)M_PI / 2.0f); // Channels 1-3
        float groupBWeight = std::sin(crossfaderPos * (float)M_PI / 2.0f); // Channels 4-6
        channelOutputSignal *= (i < 3) ? groupAWeight : groupBWeight;

        // Apply dynamic CV DUCKING
        if (inputs[DUCK_INPUT].isConnected()) {
            float duckVolts = inputs[DUCK_INPUT].getVoltage();
            float duckAmount = params[DYNAMICS_PARAM].getValue() * (duckVolts / 10.0f);
            
            float duckAtten = 1.0f - math::clamp(duckAmount, 0.0f, 1.0f);
            channelOutputSignal *= duckAtten;
        }

        // VEL_INPUT continuously modulates amplitude every sample rather than being
        // sampled once at trigger time -- e.g. patch an LFO in for tremolo, or an
        // envelope for auto-swell, applied uniformly to all 6 channels like a shared VCA.
        if (inputs[VEL_INPUT].isConnected()) {
            float monoVelMod = math::clamp(inputs[VEL_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
            channelOutputSignal *= monoVelMod;
        }

        // 3. Write pure audio straight to the output jacks! Always write, even if
        // this individual channel jack isn't patched -- the FX/master-bus stage
        // reads this value back to build the Master L/R sum, which should work
        // standalone without every individual channel also needing to be patched.
        outputs[CH1_OUTPUT + i].setVoltage(channelOutputSignal * 5.0f); // 5V Eurorack peak-to-peak audio output

        lights[CH1_LED + i].setBrightness(voice.env);
    }
}
