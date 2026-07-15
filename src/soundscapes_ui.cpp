#include "soundscapes.hpp"
#include "plugin.hpp"
#include <cstring>

/**
 * Robust Font Loading Helper
 * Interrogates asset directories to guarantee execution safety
 */
static std::shared_ptr<Font> loadRobustFont() {
    std::shared_ptr<Font> f = nullptr;
    f = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/RobotoMono-Regular.ttf"));
    if (f) return f;
    f = APP->window->loadFont(asset::plugin(pluginInstance, "res/font/RobotoMono-Regular.ttf"));
    if (f) return f;
    f = APP->window->loadFont(asset::plugin(pluginInstance, "res/RobotoMono-Regular.ttf"));
    return f;
}

/**
 * Fake-Bold Text Helper
 * No bold weight of the loaded font is available, so this thickens strokes by
 * drawing the text twice with a small offset -- used everywhere labels are drawn.
 * Caller must already have set font, size, alignment, and fill color.
 */
static void nvgTextBold(NVGcontext* vg, float x, float y, const char* text, const char* end = NULL) {
    nvgText(vg, x, y, text, end);
    nvgText(vg, x + 0.4f, y, text, end);
}

/**
 * Custom base class for robust parameter clicks in VCV Rack v2
 */
struct SoundscapesButton : app::ParamWidget {
    bool momentary = false;

    void onButton(const event::Button& e) override {
        ParamWidget::onButton(e);
        if (getParamQuantity()) {
            if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
                if (momentary) {
                    getParamQuantity()->setValue(1.0f);
                } else {
                    float newVal = (getParamQuantity()->getValue() > 0.5f) ? 0.0f : 1.0f;
                    getParamQuantity()->setValue(newVal);
                }
                e.consume(this);
            } else if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT) {
                if (momentary) {
                    getParamQuantity()->setValue(0.0f);
                }
                e.consume(this);
            }
        }
    }
};

/**
 * 1. Custom Smoked Bronze Channel Display Widget (with 60Hz UI parameter HUD change-checking)
 */
struct OpaqueDisplay : Widget {
    Soundscapes* module = nullptr;
    int channelId = 0; // Range 0 - 7
    std::shared_ptr<Font> font;

    OpaqueDisplay() {
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        Widget::onButton(e);
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (module && channelId < 6) {
                module->handleFocusToggle(channelId);
            }
            e.consume(this);
        }
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        bool isFocused = (module->focusedChannel == channelId);
        
        // Draw physical glowing aura/outline if focused
        if (isFocused) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, -2.0f, -2.0f, box.size.x + 4.0f, box.size.y + 4.0f, 5.0f);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Bright amber
            nvgStrokeWidth(args.vg, 2.0f);
            nvgStroke(args.vg);

            // Diffusion blur glow
            NVGpaint glowPaint = nvgBoxGradient(args.vg, -4.0f, -4.0f, box.size.x + 8.0f, box.size.y + 8.0f, 6.0f, 4.0f, nvgRGBA(0xff, 0x9d, 0x00, 0x7f), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, -6.0f, -6.0f, box.size.x + 12.0f, box.size.y + 12.0f, 7.0f);
            nvgFillPaint(args.vg, glowPaint);
            nvgFill(args.vg);
        }

        bool shouldFlash = isFocused && module->displayFlashState;
        if (shouldFlash) return;

        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 21.0f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xdf));

            char text[16];

            // 1. Detect fader edits for this channel at the 60Hz UI rate (channelId 6/7
            // are the master L/R display slots, which have no fader of their own)
            if (channelId < 6) {
                float currFader = module->params[Soundscapes::FADER1_PARAM + channelId].getValue();
                static float lastFader[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
                if (lastFader[channelId] >= 0.0f && fabs(currFader - lastFader[channelId]) > 0.001f) {
                    module->displayValueTimer[channelId] = 1.5f;
                    module->displayValue[channelId] = currFader;
                    module->displayType[channelId] = 0;
                }
                lastFader[channelId] = currFader;
            }

            // 2. Detect global knob edits (RATE to DYNAMICS) -- spell the knob's current
            // function name across all 8 displays (one character each) so its role is
            // unambiguous no matter which mode/patch state it's operating under.
            for (int k = 0; k < 6; k++) {
                float currKnob = module->params[Soundscapes::RATE_PARAM + k].getValue();
                static float lastKnob[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
                if (lastKnob[k] >= 0.0f && fabs(currKnob - lastKnob[k]) > 0.001f) {
                    int paramId = Soundscapes::RATE_PARAM + k;
                    const char* fn = module->macroFunctionName(paramId);
                    snprintf(module->macroFunctionText, sizeof(module->macroFunctionText), "%s", fn);
                    module->macroFunctionActive = module->isMacroActive(paramId);
                    for (int c = 0; c < 8; c++) {
                        module->displayValueTimer[c] = 1.5f;
                        module->displayType[c] = 4;
                    }
                }
                lastKnob[k] = currKnob;
            }

            // 3. Detect root note edits
            float currRoot = module->params[Soundscapes::ROOT_PARAM].getValue();
            static float lastRoot = -1.0f;
            if (lastRoot >= 0.0f && fabs(currRoot - lastRoot) > 0.001f) {
                module->displayValueTimer[channelId] = 1.5f;
                module->displayValue[channelId] = currRoot;
                module->displayType[channelId] = 1;
            }
            lastRoot = currRoot;

            // 4. Detect scale edits
            float currScale = module->params[Soundscapes::SCALE_PARAM].getValue();
            static float lastScale = -1.0f;
            if (lastScale >= 0.0f && fabs(currScale - lastScale) > 0.001f) {
                module->displayValueTimer[channelId] = 1.5f;
                module->displayValue[channelId] = currScale;
                module->displayType[channelId] = 2;
            }
            lastScale = currScale;

            // 5. Detect 3-way Mode Switch edits
            float currMode = module->params[Soundscapes::MODE_PARAM].getValue();
            static float lastMode = -1.0f;
            if (lastMode >= 0.0f && fabs(currMode - lastMode) > 0.01f) {
                module->displayValueTimer[channelId] = 1.5f;
                module->displayValue[channelId] = currMode;
                module->displayType[channelId] = 3; 
            }
            lastMode = currMode;

            // Decrease timer at UI rate (1/60th of a second per frame)
            if (module->displayValueTimer[channelId] > 0.0f) {
                module->displayValueTimer[channelId] -= 1.0f / 60.0f; 
            }

            // Route standard channel values vs temporary HUD values
            if (module->displayValueTimer[channelId] > 0.0f) {
                if (module->displayType[channelId] == 1) {
                    int r = (int)std::round(module->displayValue[channelId] * 11.0f);
                    r = clamp(r, 0, 11);
                    const char* notes[12] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
                    snprintf(text, sizeof(text), "%s", notes[r]);
                } else if (module->displayType[channelId] == 2) {
                    int s = (int)std::round(module->displayValue[channelId] * 4.0f);
                    s = clamp(s, 0, 4);
                    const char* scalesText[5] = {"MA", "MI", "PE", "DO", "PH"};
                    snprintf(text, sizeof(text), "%s", scalesText[s]);
                } else if (module->displayType[channelId] == 3) {
                    int m = (int)std::round(module->displayValue[channelId]);
                    m = clamp(m, 0, 2);
                    const char* modesText[3] = {"VO", "WA", "DR"};
                    snprintf(text, sizeof(text), "%s", modesText[m]);
                } else if (module->displayType[channelId] == 4) {
                    int len = (int)strlen(module->macroFunctionText);
                    char c = (channelId < len) ? module->macroFunctionText[channelId] : ' ';
                    snprintf(text, sizeof(text), "%c", c);
                } else {
                    int pct = (int)std::round(module->displayValue[channelId] * 99.0f);
                    pct = clamp(pct, 0, 99);
                    snprintf(text, sizeof(text), "%02d", pct);
                }
            } else {
                if (channelId == 6) {
                    snprintf(text, sizeof(text), "L");
                } else if (channelId == 7) {
                    snprintf(text, sizeof(text), "R");
                } else {
                    snprintf(text, sizeof(text), "%d", channelId + 1);
                }
            }
            nvgTextBold(args.vg, box.size.x / 2.0f, box.size.y / 2.0f + 1.5f, text, NULL);
        }
    }
};

/**
 * 2. Procedural Step Sequencer Pad Widget
 */
struct StepPadWidget : app::SvgSwitch {
    int padId = 0; // Range 0 - 15

    StepPadWidget() {
        momentary = false;
        box.size = Vec(24.0f, 20.0f);
    }

    void onButton(const event::Button& e) override {
        Soundscapes* mod = dynamic_cast<Soundscapes*>(this->module);
        if (mod && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && !mod->chordModeActive) {
            bool isChord = (padId >= 8);
            int stepIndex = isChord ? (padId - 8) : padId;
            SequencerTrack& track = isChord ? mod->chordTrack : mod->melodyTrack;

            if (mod->noteModeActive) {
                // NOTE mode: cycle this step's own melodic override up one scale degree
                // each click (0-13, two octaves), then clear back to "use the channel's
                // fixed harmonic role" on wrap -- lets a step carry real melodic movement
                // independent of whatever channel it happens to target.
                StepData& step = track.steps[stepIndex];
                if (!step.noteOverride) {
                    step.noteOverride = true;
                    step.noteDegreeOffset = 0;
                } else if (step.noteDegreeOffset < 13) {
                    step.noteDegreeOffset++;
                } else {
                    step.noteOverride = false;
                    step.noteDegreeOffset = 0;
                }

                char buf[9];
                if (step.noteOverride) {
                    snprintf(buf, sizeof(buf), "%c%d:DG%d", isChord ? 'C' : 'M', stepIndex + 1, step.noteDegreeOffset);
                } else {
                    snprintf(buf, sizeof(buf), "%c%dCLEAR", isChord ? 'C' : 'M', stepIndex + 1);
                }
                snprintf(mod->macroFunctionText, sizeof(mod->macroFunctionText), "%s", buf);
                mod->macroFunctionActive = step.noteOverride;
                for (int c = 0; c < 8; c++) {
                    mod->displayValueTimer[c] = 1.5f;
                    mod->displayType[c] = 4;
                }
                e.consume(this);
                return;
            }

            if (mod->shiftActive && mod->focusedChannel != -1) {
                // SHFT+click while a channel is focused reassigns this step's target
                // channel instead of toggling it -- decouples timing position from which
                // harmonic role it plays. A normal click still just toggles active/inactive.
                track.steps[stepIndex].targetChannel = (int8_t)mod->focusedChannel;

                // Flash a confirmation across the 7-segment displays, e.g. "M3->C5"
                char buf[9];
                snprintf(buf, sizeof(buf), "%c%d-C%d", isChord ? 'C' : 'M', stepIndex + 1, mod->focusedChannel + 1);
                snprintf(mod->macroFunctionText, sizeof(mod->macroFunctionText), "%s", buf);
                mod->macroFunctionActive = true;
                for (int c = 0; c < 8; c++) {
                    mod->displayValueTimer[c] = 1.5f;
                    mod->displayType[c] = 4;
                }
                e.consume(this);
                return;
            }
        }
        SvgSwitch::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool stepActive = false;
        bool isPlayhead = false;

        if (module) {
            if (padId < 6) {
                stepActive = module->params[Soundscapes::STEP_PARAM_START + padId].getValue() > 0.5f;
                isPlayhead = (module->melodyTrack.playhead == padId) && module->isPlaying;
            } else {
                int chordStep = padId - 6;
                stepActive = module->params[Soundscapes::STEP_PARAM_START + padId].getValue() > 0.5f;
                isPlayhead = (module->chordTrack.playhead == chordStep) && module->isPlaying;
            }
        }

        // Draw soft button shadow
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 1.0f, box.size.x, box.size.y, 3.5f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x0c));
        nvgFill(args.vg);

        // Draw button body
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 3.5f);

        if (module && module->chordModeActive) {
            // In CHRD mode the pads are a 16-slot option selector, not step toggles --
            // tint purple (matching the CHRD button) so it reads as a different mode.
            if (stepActive) {
                nvgFillColor(args.vg, nvgRGBA(155, 89, 182, 255)); // Vivid Royal Purple
            } else {
                nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            }
        } else if (isPlayhead) {
            nvgFillColor(args.vg, nvgRGBA(0x2e, 0xcc, 0x71, 0xff)); // Active green playhead
        } else if (stepActive) {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Active step amber
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // Clean white unlit pad
        }
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xd5, 0xcf, 0xc5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Small dark dot in the top-right corner marks a step whose target channel has
        // been reassigned away from its own index (timing decoupled from harmonic role).
        if (module && !module->chordModeActive) {
            bool isChord = (padId >= 8);
            int stepIndex = isChord ? (padId - 8) : padId;
            const SequencerTrack& track = isChord ? module->chordTrack : module->melodyTrack;
            if (track.steps[stepIndex].targetChannel != stepIndex % 6) {
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, box.size.x - 4.0f, 4.0f, 2.0f);
                nvgFillColor(args.vg, nvgRGBA(0x2b, 0x28, 0x24, 0xff));
                nvgFill(args.vg);
            }
            // Small colored dot in the bottom-left corner marks a step carrying its own
            // melodic override (plays its own note instead of the target channel's role).
            if (track.steps[stepIndex].noteOverride) {
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, 4.0f, box.size.y - 4.0f, 2.0f);
                nvgFillColor(args.vg, nvgRGBA(0x34, 0x98, 0xdb, 0xff)); // Blue
                nvgFill(args.vg);
            }
        }
    }
};

/**
 * 3. Procedural Slide Fader Handle
 */
struct SoundscapesFader : app::SvgSlider {
    SoundscapesFader() {
        box.size = Vec(14.0f, 46.0f); // Shorter travel
        speed = 4.0f;                 // Snappier mouse dragging
    }

    void draw(const DrawArgs& args) override {
        // Draw fader track groove -- plain rect, no rounded end-caps (those read as
        // small "nips" poking out top/bottom on a groove this thin)
        nvgBeginPath(args.vg);
        nvgRect(args.vg, box.size.x / 2.0f - 2.0f, 0.0f, 4.0f, box.size.y);
        nvgFillColor(args.vg, nvgRGBA(13, 12, 11, 255)); // #0d0c0b
        nvgFill(args.vg);

        // Position of cap handle relative to travel range
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.8f;
        float handleHeight = 16.0f;
        float handleY = (1.0f - value) * (box.size.y - handleHeight);

        // Draw fader handle body
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, handleY, box.size.x, handleHeight, 2.0f);
        nvgFillColor(args.vg, nvgRGBA(0xfa, 0xf9, 0xf6, 0xff)); // Silver-cream
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcc, 0xc4, 0xb5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Center black indicator line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 2.0f, handleY + handleHeight / 2.0f);
        nvgLineTo(args.vg, box.size.x - 2.0f, handleY + handleHeight / 2.0f);
        nvgStrokeColor(args.vg, nvgRGBA(0x1a, 0x1a, 0x1a, 0xff));
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStroke(args.vg);
    }
};

/**
 * 4. Procedural Utility/Performance Buttons (Enhanced high-contrast vivid colors)
 */
struct PerformanceButtonWidget : SoundscapesButton {
    int buttonId = 0; // 0 to 7
    std::shared_ptr<Font> font;

    PerformanceButtonWidget() {
        box.size = Vec(22.0f, 18.0f);
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        // PLAY(0), SHFT(1), CHRD(4), and PROB(5) are latching mode switches; the
        // rest (including ARP/FRZ, dropped for the simple v1) are momentary.
        momentary = (buttonId != 0 && buttonId != 1 && buttonId != 4 && buttonId != 5);
        SoundscapesButton::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool litState = false;

        if (module) {
            if (buttonId == 0) litState = module->isPlaying;       // PLAY Button
            if (buttonId == 1) litState = module->shiftActive;     // SHFT Button
            if (buttonId == 4) litState = module->chordModeActive; // CHRD Button (STEP <-> CHRD mode)
            if (buttonId == 5) litState = module->noteModeActive;  // PROB Button (PROB <-> NOTE mode)
        }

        // Soft shadow
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 1.0f, box.size.x, box.size.y, 2.5f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x0c));
        nvgFill(args.vg);

        // Button Body
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 2.5f);

        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        if (litState || value > 0.5f) {
            // Two colors only: green for persistent mode/state toggles (PLAY, SHFT,
            // ARP, FRZ, CHRD, PROB), amber for momentary one-shot actions (SAVE, RCL).
            // Color variety is reserved for the FX section instead.
            bool isStateToggle = (buttonId <= 5);
            if (isStateToggle) {
                nvgFillColor(args.vg, nvgRGBA(0x2e, 0xcc, 0x71, 0xff)); // Green
            } else {
                nvgFillColor(args.vg, nvgRGBA(0xf1, 0xc4, 0x0f, 0xff)); // Amber
            }
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // White unlit
        }
        nvgFill(args.vg);

        // Bold white stroke to highlight active state
        if (litState || value > 0.5f) {
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 255));
            nvgStrokeWidth(args.vg, 1.5f);
        } else {
            nvgStrokeColor(args.vg, nvgRGBA(0xcc, 0xc4, 0xb6, 0xff));
            nvgStrokeWidth(args.vg, 1.0f);
        }
        nvgStroke(args.vg);

        // Render Dynamic Text Label inside the button
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 7.0f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, (litState || value > 0.5f) ? nvgRGBA(0xff, 0xff, 0xff, 0xff) : nvgRGBA(0x3a, 0x35, 0x2e, 0xff));

            const char* labels[8] = {"PLAY", "SHFT", "ARP", "FRZ", "CHRD", "PROB", "SAVE", "RCL"};
            const char* labelText = labels[buttonId];
            if (buttonId == 4) {
                labelText = (litState || value > 0.5f) ? "CHRD" : "STEP";
            } else if (buttonId == 5) {
                labelText = (litState || value > 0.5f) ? "NOTE" : "PROB";
            }
            nvgTextBold(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, labelText, NULL);
        }
    }
};

/**
 * 5. Procedural Large White Knob Widget (No external SVG dependencies)
 */
struct SoundscapesKnob : app::Knob {
    SoundscapesKnob() {
        box.size = Vec(26.0f, 26.0f); // Large diameter: 26px
    }

    void draw(const DrawArgs& args) override {
        // For the 6 macro knobs (RATE..DYNAMICS), check whether this knob currently
        // has any audible effect given the module's mode/patch state, and tint it
        // accordingly so an inert knob doesn't leave the user wondering why turning
        // it did nothing.
        bool active = true;
        if (getParamQuantity() && getParamQuantity()->module) {
            Soundscapes* mod = dynamic_cast<Soundscapes*>(getParamQuantity()->module);
            int paramId = getParamQuantity()->paramId;
            if (mod && paramId >= Soundscapes::RATE_PARAM && paramId <= Soundscapes::DYNAMICS_PARAM) {
                active = mod->isMacroActive(paramId);
            }
        }

        NVGcolor bodyFill = active ? nvgRGBA(0x2b, 0x28, 0x24, 0xff) : nvgRGBA(0xfa, 0xf9, 0xf6, 0xff);
        NVGcolor bodyStroke = active ? nvgRGBA(0x00, 0x00, 0x00, 0xff) : nvgRGBA(0xcb, 0xc4, 0xb5, 0xff);
        NVGcolor indicatorColor = active ? nvgRGBA(0xff, 0xff, 0xff, 0xff) : nvgRGBA(0x60, 0x55, 0x48, 0xff);

        int accentGroup = 0;
        if (getParamQuantity() && getParamQuantity()->module) {
            Soundscapes* mod = dynamic_cast<Soundscapes*>(getParamQuantity()->module);
            if (mod) accentGroup = mod->macroAccentGroup(getParamQuantity()->paramId);
        }

        // Drop shadow
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 13.0f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x08));
        nvgFill(args.vg);

        // Accent ring: matches the associated FX button's color when that bus is
        // currently selected, for easy visual pairing of "this knob shapes that FX".
        if (accentGroup != 0) {
            NVGcolor accentColor;
            if (accentGroup == 1) accentColor = nvgRGBA(0x34, 0x98, 0xdb, 0xff);      // FM: Blue
            else if (accentGroup == 2) accentColor = nvgRGBA(0x1a, 0xbc, 0x9c, 0xff); // DELAY: Teal
            else if (accentGroup == 3) accentColor = nvgRGBA(0xff, 0x9d, 0x00, 0xff); // REVERB: Amber
            else accentColor = nvgRGBA(0xe9, 0x1e, 0x63, 0xff);                       // FILTER: Magenta

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 13.5f);
            nvgStrokeColor(args.vg, accentColor);
            nvgStrokeWidth(args.vg, 2.0f);
            nvgStroke(args.vg);
        }

        // Knob Body
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 12.0f);
        nvgFillColor(args.vg, bodyFill);
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, bodyStroke);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Indicator line rotated dynamically by parameter value
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        float angle = -120.0f + value * 240.0f; // VCV standard knob angle
        float rad = angle * M_PI / 180.0f;
        float cx = box.size.x / 2.0f;
        float cy = box.size.y / 2.0f;
        
        float px = cx + std::sin(rad) * 10.0f;
        float py = cy - std::cos(rad) * 10.0f;

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + std::sin(rad) * 4.0f, cy - std::cos(rad) * 4.0f);
        nvgLineTo(args.vg, px, py);
        nvgStrokeColor(args.vg, indicatorColor);
        nvgStrokeWidth(args.vg, 1.6f);
        nvgStroke(args.vg);
    }
};

/**
 * 6. Procedural Small White Knob Widget (Diagonal Quantizer Knobs)
 */
struct SoundscapesSmallKnob : SoundscapesKnob {
    SoundscapesSmallKnob() {
        box.size = Vec(20.0f, 20.0f); // Small diameter: 20px
    }

    void draw(const DrawArgs& args) override {
        // Drop shadow
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 10.0f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x06));
        nvgFill(args.vg);

        // Knob Body
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 9.0f);
        nvgFillColor(args.vg, nvgRGBA(0xfa, 0xf9, 0xf6, 0xff)); // Cream-white
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcb, 0xc4, 0xb5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Indicator line rotated dynamically by parameter value
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        float angle = -120.0f + value * 240.0f;
        float rad = angle * M_PI / 180.0f;
        float cx = box.size.x / 2.0f;
        float cy = box.size.y / 2.0f;
        
        float px = cx + std::sin(rad) * 7.5f;
        float py = cy - std::cos(rad) * 7.5f;

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + std::sin(rad) * 3.0f, cy - std::cos(rad) * 3.0f);
        nvgLineTo(args.vg, px, py);
        nvgStrokeColor(args.vg, nvgRGBA(0x60, 0x55, 0x48, 0xff));
        nvgStrokeWidth(args.vg, 1.4f);
        nvgStroke(args.vg);
    }
};

/**
 * 7. Procedural MODE Selection Window (Beautiful 3-Way Selector Switch)
 * Overrides click logic to cycle smoothly across 3 states (0, 1, 2)
 */
struct ModeThreeWaySwitch : app::ParamWidget {
    std::shared_ptr<Font> font;

    ModeThreeWaySwitch() {
        box.size = Vec(14.0f, 36.0f);
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        ParamWidget::onButton(e);
        // Switches across three clean positions (0, 1, 2) without clipping
        if (getParamQuantity()) {
            if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
                float val = getParamQuantity()->getValue();
                float newVal = val + 1.0f;
                if (newVal > 2.0f) newVal = 0.0f;
                getParamQuantity()->setValue(newVal);
                e.consume(this);
            }
        }
    }

    void draw(const DrawArgs& args) override {
        // Draw track groove
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, box.size.x / 2.0f - 2.0f, 2.0f, 4.0f, box.size.y - 4.0f, 1.5f);
        nvgFillColor(args.vg, nvgRGBA(13, 12, 11, 255));
        nvgFill(args.vg);

        // Snap discrete points: 0.0 (top), 1.0 (middle), 2.0 (bottom)
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        float switchHeight = 10.0f;
        float handleY = (value / 2.0f) * (box.size.y - switchHeight);

        // Draw elegant chrome slide handle
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, handleY, box.size.x, switchHeight, 1.5f);
        nvgFillColor(args.vg, nvgRGBA(220, 220, 220, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 80, 255));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Draw selection markers V, W, D next to the switch positions inside the bronze screen
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 7.5f);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xdf));

            nvgTextBold(args.vg, -6.0f, 5.0f, "V", NULL);
            nvgTextBold(args.vg, -6.0f, box.size.y / 2.0f, "W", NULL);
            nvgTextBold(args.vg, -6.0f, box.size.y - 5.0f, "D", NULL);
        }
    }
};

/**
 * 8. Procedural FX Selection Buttons
 */
struct FXButtonWidget : SoundscapesButton {
    std::string label;
    std::shared_ptr<Font> font;

    FXButtonWidget() {
        momentary = false;
        box.size = Vec(24.0f, 20.0f);
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        // Corrected: If shift is active, disable button clicks completely (fixes hidden latching)
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        if (module && module->shiftActive) {
            e.consume(this);
            return;
        }
        SoundscapesButton::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool isActive = false;

        if (module) {
            if (label == "FM" && module->activeFaderState == FADER_FM_SEND) isActive = true;
            if (label == "DELAY" && module->activeFaderState == FADER_DELAY_SEND) isActive = true;
            if (label == "REVERB" && module->activeFaderState == FADER_REVERB_SEND) isActive = true;
            if (label == "FILTER" && module->activeFaderState == FADER_FILTER_SEND) isActive = true;
        }

        // Drop shadow
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 1.0f, box.size.x, box.size.y, 3.5f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x0c));
        nvgFill(args.vg);

        // Button Body
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 3.5f);

        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        if (isActive) {
            // Each FX bus gets its own distinct color, shared with its associated
            // macro knob's accent ring for easy visual identification.
            if (label == "FM") {
                nvgFillColor(args.vg, nvgRGBA(0x34, 0x98, 0xdb, 0xff)); // Blue
            } else if (label == "DELAY") {
                nvgFillColor(args.vg, nvgRGBA(0x1a, 0xbc, 0x9c, 0xff)); // Teal
            } else if (label == "REVERB") {
                nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Amber
            } else {
                nvgFillColor(args.vg, nvgRGBA(0xe9, 0x1e, 0x63, 0xff)); // Magenta (FILTER)
            }
        } else if (value > 0.5f) {
            nvgFillColor(args.vg, nvgRGBA(0xee, 0xee, 0xee, 0xff)); // Pressed grey
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // Idle white
        }
        nvgFill(args.vg);

        // Highlight stroke for active selection
        if (isActive) {
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 255));
            nvgStrokeWidth(args.vg, 1.5f);
        } else {
            nvgStrokeColor(args.vg, nvgRGBA(0xcc, 0xc4, 0xb6, 0xff));
            nvgStrokeWidth(args.vg, 1.0f);
        }
        nvgStroke(args.vg);

        // Centered Button Label text drawn using verified monospaced font
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 7.0f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, isActive ? nvgRGBA(0xff, 0xff, 0xff, 0xff) : nvgRGBA(0x5c, 0x53, 0x46, 0xff));
            nvgTextBold(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, label.c_str(), NULL);
        }
    }
};

/**
 * 9. Custom Layer Widget to Programmatically Render Label Layers
 */
struct FaceplateLabels : Widget {
    std::shared_ptr<Font> font;

    FaceplateLabels() {
        font = loadRobustFont();
        box.size = Vec(420.0f, 380.0f);
    }

    void draw(const DrawArgs& args) override {
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, nvgRGBA(0x5c, 0x53, 0x46, 0xff)); // Clean charcoal
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            
            // A. Draw Module Main Header
            nvgFontSize(args.vg, 13.0f);
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, nvgRGBA(0x11, 0x11, 0x11, 0xff)); // Dark bold
            nvgTextBold(args.vg, 232.5f, 25.0f, "SOUNDSCAPES", NULL);

            // B. Draw Module Slogan
            nvgFontSize(args.vg, 7.0f);
            nvgFillColor(args.vg, nvgRGBA(0x6d, 0x65, 0x58, 0xff)); // Cream-charcoal
            nvgTextBold(args.vg, 232.5f, 36.0f, "6-Channel Poly-Engine", NULL);

            // C. Sidebar Labels (CLK, RST, etc.)
            const char* sidebarLabels[7] = {"CLK", "RST", "V/OCT", "GATE", "VEL", "EXT IN", "DUCK"};
            for (int i = 0; i < 7; i++) {
                float y = SoundscapesCoords::SIDEBAR_Y_START + (i * SoundscapesCoords::SIDEBAR_Y_SPACING);
                nvgFontSize(args.vg, 7.0f);
                if (i == 5) nvgFillColor(args.vg, nvgRGBA(0xd4, 0x7d, 0x00, 0xff)); // Orange for EXT IN
                else if (i == 6) nvgFillColor(args.vg, nvgRGBA(0xd0, 0x02, 0x1b, 0xff)); // Red for DUCK
                else nvgFillColor(args.vg, nvgRGBA(0x7d, 0x71, 0x60, 0xff));
                nvgTextBold(args.vg, SoundscapesCoords::SIDEBAR_JACK_X, y + 14.0f, sidebarLabels[i], NULL);
            }
            nvgFillColor(args.vg, nvgRGBA(0x5c, 0x53, 0x46, 0xff)); // Reset to standard charcoal

            // D. Output Jack Column Numbers (1 to 6), then L/R for the master outs
            for (int i = 0; i < 6; i++) {
                float x = SoundscapesCoords::CH_COLS[i];
                nvgFontSize(args.vg, 7.0f);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW1_JACK_Y + 16.0f, std::to_string(i + 1).c_str(), NULL);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW1_DISPLAY_Y + 28.0f, std::to_string(i + 1).c_str(), NULL);
            }
            nvgFontSize(args.vg, 7.0f);
            nvgTextBold(args.vg, SoundscapesCoords::CH_COLS[6], SoundscapesCoords::ROW1_JACK_Y + 16.0f, "L", NULL);
            nvgTextBold(args.vg, SoundscapesCoords::CH_COLS[6], SoundscapesCoords::ROW1_DISPLAY_Y + 28.0f, "L", NULL);
            nvgTextBold(args.vg, SoundscapesCoords::CH_COLS[7], SoundscapesCoords::ROW1_JACK_Y + 16.0f, "R", NULL);
            nvgTextBold(args.vg, SoundscapesCoords::CH_COLS[7], SoundscapesCoords::ROW1_DISPLAY_Y + 28.0f, "R", NULL);

            // E. Fader Numbers (1 to 6)
            for (int i = 0; i < 6; i++) {
                float x = SoundscapesCoords::GRID_COLS[i];
                nvgFontSize(args.vg, 7.0f);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW3_FADER_Y + 34.0f, std::to_string(i + 1).c_str(), NULL);
            }

            // F. Macro Knob Labels
            const char* knobLabels[6] = {"RATE", "DENSITY", "TIMBRE", "TEXTURE", "SPREAD", "DYNAMICS"};
            for (int i = 0; i < 6; i++) {
                nvgFontSize(args.vg, 7.5f);
                nvgTextBold(args.vg, SoundscapesCoords::KNOB_COLS[i], SoundscapesCoords::ROW2_KNOB_Y + 21.0f, knobLabels[i], NULL);
            }

            // G. Root & Scale Labels
            nvgFontSize(args.vg, 6.5f);
            nvgTextBold(args.vg, SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y + 21.0f, "ROOT", NULL);
            nvgTextBold(args.vg, SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y + 21.0f, "SCALE", NULL);

            // H. Melody & Chord Labels
            nvgFontSize(args.vg, 6.5f);
            for (int i = 0; i < 8; i++) {
                float x = SoundscapesCoords::GRID_COLS[i];
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW4_MELODY_PAD_Y + 18.0f, std::to_string(i + 1).c_str(), NULL);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW4_CHORD_PAD_Y + 18.0f, std::to_string(i + 9).c_str(), NULL);
            }

            // Section Labels
            nvgFontSize(args.vg, 7.5f);
            nvgTextBold(args.vg, 198.0f, 315.0f, "MELODY", NULL);
            nvgTextBold(args.vg, 198.0f, 363.0f, "CHORD", NULL);
        }
    }
};

/**
 * 10. Master ModuleWidget Panel Setup
 */
struct SoundscapesWidget : ModuleWidget {
    SoundscapesWidget(Soundscapes* module) {
        setModule(module);
        
        // Load the finalized symmetrical vector faceplate panel
        setPanel(createPanel(asset::plugin(pluginInstance, "res/soundscapes-mm.svg")));

        // Add the programmatic label overlay directly on top of the SVG panel
        FaceplateLabels* labelOverlay = new FaceplateLabels();
        addChild(labelOverlay);

        // --- I. Left Sidebar Inputs & LEDs ---
        for (int i = 0; i < Soundscapes::NUM_INPUTS; i++) {
            float y = SoundscapesCoords::SIDEBAR_Y_START + (i * SoundscapesCoords::SIDEBAR_Y_SPACING);
            addInput(createInputCentered<PJ301MPort>(Vec(SoundscapesCoords::SIDEBAR_JACK_X, y), module, i));
            addChild(createLightCentered<MediumLight<GreenLight>>(Vec(SoundscapesCoords::SIDEBAR_LED_X, y), module, i));
        }

        // --- II. Row 1: Outputs, LED Indicators, & Opaque Displays ---
        // 6 synth-voice channels (unchanged style/position)
        for (int i = 0; i < 6; i++) {
            float x = SoundscapesCoords::CH_COLS[i];
            addOutput(createOutputCentered<PJ301MPort>(Vec(x, SoundscapesCoords::ROW1_JACK_Y), module, Soundscapes::CH1_OUTPUT + i));
            addChild(createLightCentered<MediumLight<GreenLight>>(Vec(x, SoundscapesCoords::ROW1_LED_Y), module, Soundscapes::CH1_LED + i));

            // Custom Display overlays
            OpaqueDisplay* display = new OpaqueDisplay();
            display->box.pos = Vec(x - 14.0f, SoundscapesCoords::ROW1_DISPLAY_Y - 20.0f);
            display->box.size = Vec(28.0f, 40.0f);
            display->module = module;
            display->channelId = i;
            addChild(display);
        }

        // MASTER L/R stereo sum, occupying the jack positions that used to be
        // channels 7/8 -- no panel repositioning needed, just a different function.
        addOutput(createOutputCentered<PJ301MPort>(Vec(SoundscapesCoords::CH_COLS[6], SoundscapesCoords::ROW1_JACK_Y), module, Soundscapes::MASTER_L_OUTPUT));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(SoundscapesCoords::CH_COLS[6], SoundscapesCoords::ROW1_LED_Y), module, Soundscapes::MASTER_L_LED));

        addOutput(createOutputCentered<PJ301MPort>(Vec(SoundscapesCoords::CH_COLS[7], SoundscapesCoords::ROW1_JACK_Y), module, Soundscapes::MASTER_R_OUTPUT));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(SoundscapesCoords::CH_COLS[7], SoundscapesCoords::ROW1_LED_Y), module, Soundscapes::MASTER_R_LED));

        {
            OpaqueDisplay* masterLDisplay = new OpaqueDisplay();
            masterLDisplay->box.pos = Vec(SoundscapesCoords::CH_COLS[6] - 14.0f, SoundscapesCoords::ROW1_DISPLAY_Y - 20.0f);
            masterLDisplay->box.size = Vec(28.0f, 40.0f);
            masterLDisplay->module = module;
            masterLDisplay->channelId = 6; // 6/7 are the master L/R display slots
            addChild(masterLDisplay);

            OpaqueDisplay* masterRDisplay = new OpaqueDisplay();
            masterRDisplay->box.pos = Vec(SoundscapesCoords::CH_COLS[7] - 14.0f, SoundscapesCoords::ROW1_DISPLAY_Y - 20.0f);
            masterRDisplay->box.size = Vec(28.0f, 40.0f);
            masterRDisplay->module = module;
            masterRDisplay->channelId = 7;
            addChild(masterRDisplay);
        }

        // --- III. Row 2: Centralized Synth Deck (Using discrete 3-way switch) ---
        addParam(createParamCentered<ModeThreeWaySwitch>(Vec(SoundscapesCoords::MODE_X, SoundscapesCoords::MODE_Y), module, Soundscapes::MODE_PARAM));

        // 2x2 FX Button Group
        FXButtonWidget* fmBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[0], SoundscapesCoords::FX_ROWS[0]), module, Soundscapes::FM_PARAM);
        fmBtn->label = "FM";
        addParam(fmBtn);

        FXButtonWidget* dlyBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[1], SoundscapesCoords::FX_ROWS[0]), module, Soundscapes::DELAY_PARAM);
        dlyBtn->label = "DELAY";
        addParam(dlyBtn);

        FXButtonWidget* revBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[0], SoundscapesCoords::FX_ROWS[1]), module, Soundscapes::REVERB_PARAM);
        revBtn->label = "REVERB";
        addParam(revBtn);

        FXButtonWidget* fltBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[1], SoundscapesCoords::FX_ROWS[1]), module, Soundscapes::FILTER_PARAM);
        fltBtn->label = "FILTER";
        addParam(fltBtn);

        // 6 Large Parameter Knobs
        for (int i = 0; i < 6; i++) {
            addParam(createParamCentered<SoundscapesKnob>(Vec(SoundscapesCoords::KNOB_COLS[i], SoundscapesCoords::ROW2_KNOB_Y), module, Soundscapes::RATE_PARAM + i));
        }

        // --- IV. Row 3: Mixer Faders & Diagonal Quantizer Knobs ---
        // 6 Volume/Send Faders centered over track positions
        for (int i = 0; i < 6; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];
            addParam(createParamCentered<SoundscapesFader>(Vec(x, SoundscapesCoords::ROW3_FADER_Y), module, Soundscapes::FADER1_PARAM + i));
        }

        // Diagonal Large Quantizers on Columns 9 and 10
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y), module, Soundscapes::ROOT_PARAM));
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y), module, Soundscapes::SCALE_PARAM));

        // --- V. Row 4: Step Sequencer Pads & Performance Block ---
        // 16 Step Pad triggers (Columns 1-8): 8 melody + 8 chord, independent of the
        // 6 real channels -- steps and channels are decoupled (target-channel
        // reassignment), so more steps than channels is intentional.
        for (int i = 0; i < 8; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];
            
            // Melody step button (Row 1)
            StepPadWidget* melPad = createParamCentered<StepPadWidget>(Vec(x, SoundscapesCoords::ROW4_MELODY_PAD_Y), module, Soundscapes::STEP_PARAM_START + i);
            melPad->padId = i;
            addParam(melPad);

            // Chord step button (Row 2)
            StepPadWidget* chdPad = createParamCentered<StepPadWidget>(Vec(x, SoundscapesCoords::ROW4_CHORD_PAD_Y), module, Soundscapes::STEP_PARAM_START + 8 + i);
            chdPad->padId = 8 + i;
            addParam(chdPad);
        }

        // 2x4 Utility Button Grid on Columns 9 & 10
        for (int row = 0; row < 4; row++) {
            float y = SoundscapesCoords::ROW4_BUTTON_ROWS[row];
            int btnIndex = row * 2;

            // Column 1 Button
            PerformanceButtonWidget* btn1 = createParamCentered<PerformanceButtonWidget>(Vec(SoundscapesCoords::GRID_COLS[8], y), module, Soundscapes::PLAY_PARAM + btnIndex);
            btn1->buttonId = btnIndex;
            addParam(btn1);

            // Column 2 Button
            PerformanceButtonWidget* btn2 = createParamCentered<PerformanceButtonWidget>(Vec(SoundscapesCoords::GRID_COLS[9], y), module, Soundscapes::PLAY_PARAM + btnIndex + 1);
            btn2->buttonId = btnIndex + 1;
            addParam(btn2);
        }
    }
};

// Model definition binding class implementations to unique slug
Model* modelSoundscapes = createModel<Soundscapes, SoundscapesWidget>("soundscapes-mm");
