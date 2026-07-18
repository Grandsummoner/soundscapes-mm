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

            // 2. Detect global knob edits (RATE to DYNAMICS) -- show a colorful VU-meter
            // bargraph fill across the 8 displays instead of spelling out the knob's
            // function name. The player already knows which knob they're turning;
            // announcing it every time was distracting and served no real purpose.
            for (int k = 0; k < 6; k++) {
                float currKnob = module->params[Soundscapes::RATE_PARAM + k].getValue();
                static float lastKnob[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
                if (lastKnob[k] >= 0.0f && fabs(currKnob - lastKnob[k]) > 0.001f) {
                    for (int c = 0; c < 8; c++) {
                        module->displayValueTimer[c] = 1.5f;
                        module->displayValue[c] = currKnob;
                        module->displayType[c] = 5;
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
                } else if (module->displayType[channelId] == 5) {
                    // VU-meter bargraph: knob-turn HUD. Colorful zones (green/yellow/red)
                    // instead of a flat monotone fill, so it reads at a glance like a real
                    // level meter rather than just a generic progress bar.
                    int litCount = (int)std::round(module->displayValue[channelId] * 8.0f);
                    litCount = clamp(litCount, 0, 8);
                    if (channelId < litCount) {
                        NVGcolor segColor;
                        if (channelId < 5) segColor = nvgRGBA(0x2e, 0xcc, 0x71, 0xff);      // Green (segments 1-5)
                        else if (channelId < 7) segColor = nvgRGBA(0xf1, 0xc4, 0x0f, 0xff); // Yellow (6-7)
                        else segColor = nvgRGBA(0xe7, 0x4c, 0x3c, 0xff);                     // Red (8)
                        nvgBeginPath(args.vg);
                        nvgRoundedRect(args.vg, 4.0f, 4.0f, box.size.x - 8.0f, box.size.y - 8.0f, 2.0f);
                        nvgFillColor(args.vg, segColor);
                        nvgFill(args.vg);
                    }
                    return; // Filled block, not text -- skip the nvgTextBold call below
                } else {
                    int pct = (int)std::round(module->displayValue[channelId] * 99.0f);
                    pct = clamp(pct, 0, 99);
                    snprintf(text, sizeof(text), "%02d", pct);
                }
            } else {
                // Idle: leave dark. Channel/L/R identity is already printed once,
                // above the display -- repeating it here was redundant, and a
                // display that's only ever lit when something needs to show is a
                // clearer signal than one that's always showing something.
                return;
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
        if (mod && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (mod->saveArmed) {
                // SAVE: pressing a pad writes the current live pattern (all 6
                // channels' pitch+probability across all 16 steps) into this slot,
                // then SAVE auto-unlatches immediately.
                PatternSlot& slot = mod->slots[padId];
                for (int ch = 0; ch < 6; ch++) {
                    for (int s = 0; s < 16; s++) {
                        slot.pitch[ch][s] = mod->stepPitch[ch][s];
                        slot.prob[ch][s] = mod->stepProb[ch][s];
                    }
                }
                slot.occupied = true;
                mod->params[Soundscapes::SAVE_PARAM].setValue(0.0f);
                mod->saveArmed = false;

                char buf[9];
                snprintf(buf, sizeof(buf), "SAVE%02d", padId + 1);
                snprintf(mod->macroFunctionText, sizeof(mod->macroFunctionText), "%s", buf);
                mod->macroFunctionActive = true;
                for (int c = 0; c < 8; c++) {
                    mod->displayValueTimer[c] = 1.5f;
                    mod->displayType[c] = 4;
                }
                e.consume(this);
                return;
            }

            if (mod->rclArmed) {
                // RCL: pressing a pad loads that slot into the live pattern (if
                // occupied) and stays latched -- lets you keep punching through
                // slots to audition/perform across memory without leaving RCL.
                PatternSlot& slot = mod->slots[padId];
                if (slot.occupied) {
                    for (int ch = 0; ch < 6; ch++) {
                        for (int s = 0; s < 16; s++) {
                            mod->stepPitch[ch][s] = slot.pitch[ch][s];
                            mod->stepProb[ch][s] = slot.prob[ch][s];
                        }
                    }
                }

                char buf[9];
                snprintf(buf, sizeof(buf), slot.occupied ? "RCL%02d" : "EMPTY%02d", padId + 1);
                snprintf(mod->macroFunctionText, sizeof(mod->macroFunctionText), "%s", buf);
                mod->macroFunctionActive = slot.occupied;
                for (int c = 0; c < 8; c++) {
                    mod->displayValueTimer[c] = 1.5f;
                    mod->displayType[c] = 4;
                }
                e.consume(this);
                return;
            }

            // Normal state: pads are a passive playhead indicator only, not
            // click-to-edit -- step content is set by riding the channel faders
            // with PITCH/PROB armed (see handleFaderMapping), not by clicking pads.
            e.consume(this);
            return;
        }
        SvgSwitch::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool isPlayhead = false;
        bool slotOccupied = false;
        bool pickerMode = false;

        if (module) {
            pickerMode = module->saveArmed || module->rclArmed;
            if (pickerMode) {
                slotOccupied = module->slots[padId].occupied;
            } else {
                isPlayhead = (module->currentStep == padId);
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

        if (pickerMode) {
            // SAVE/RCL slot picker: tint by whether a saved pattern lives here.
            // SAVE tints amber (matches the SAVE button), RCL tints purple.
            bool isSave = module && module->saveArmed;
            NVGcolor occupiedColor = isSave ? nvgRGBA(0xff, 0x9d, 0x00, 0xff) : nvgRGBA(155, 89, 182, 255);
            nvgFillColor(args.vg, slotOccupied ? occupiedColor : nvgRGBA(0xff, 0xff, 0xff, 0xff));
        } else if (isPlayhead) {
            nvgFillColor(args.vg, nvgRGBA(0x2e, 0xcc, 0x71, 0xff)); // Active green playhead
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // Clean white, passive
        }
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xd5, 0xcf, 0xc5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
    }
};

/**
 * 3. Procedural Slide Fader Handle
 */
struct SoundscapesFader : app::SvgSlider {
    // Overridable so global faders (FX Return, Master Level) can carry a
    // different cap color than the 6 neutral channel faders, flagging at a
    // glance that they're not per-channel controls.
    NVGcolor capFill = nvgRGBA(0xfa, 0xf9, 0xf6, 0xff);   // Silver-cream (default)
    NVGcolor capStroke = nvgRGBA(0xcc, 0xc4, 0xb5, 0xff);

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
        nvgFillColor(args.vg, capFill);
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, capStroke);
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
 * Global fader variants -- same mechanics as SoundscapesFader, distinct caps.
 * Column 7: FX Return (wet depth of whichever FX buttons are engaged).
 * Column 8: Master Level (post-mix trim on the Master L/R output).
 * Colors kept clean/muted (not a gnarly rainbow) -- just enough contrast to
 * read as "global control", not "channel 7 / channel 8".
 */
struct FxReturnFader : SoundscapesFader {
    FxReturnFader() {
        capFill = nvgRGBA(0x8a, 0xc9, 0xc3, 0xff);   // Muted teal -- "processing"
        capStroke = nvgRGBA(0x5c, 0x9a, 0x94, 0xff);
    }
};

struct MasterLevelFader : SoundscapesFader {
    MasterLevelFader() {
        capFill = nvgRGBA(0xe6, 0xb8, 0x1e, 0xff);   // Gold/amber -- "final output"; was
                                                       // bright white, nearly identical to
                                                       // the neutral channel cap color
        capStroke = nvgRGBA(0xa8, 0x84, 0x0e, 0xff);
    }
};

/**
 * 4. Procedural Utility/Performance Buttons (Enhanced high-contrast vivid colors)
 */
struct PerformanceButtonWidget : SoundscapesButton {
    int buttonId = 0; // 0 = PITCH, 1 = PROB, 2 = SAVE, 3 = RCL
    std::shared_ptr<Font> font;

    PerformanceButtonWidget() {
        box.size = Vec(24.0f, 20.0f); // was 22x18 -- now matches FXButtonWidget size
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        // All four are latching -- SAVE's "auto-unlatch on pad press" behavior
        // happens in module logic (StepPadWidget::onButton / handleFaderMapping),
        // not by making this widget itself momentary.
        momentary = false;
        SoundscapesButton::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool litState = false;

        if (module) {
            if (buttonId == 0) litState = module->pitchArmed;
            if (buttonId == 1) litState = module->probArmed;
            if (buttonId == 2) litState = module->saveArmed;
            if (buttonId == 3) litState = module->rclArmed;
        }

        // Soft shadow
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 1.0f, box.size.x, box.size.y, 2.5f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x0c));
        nvgFill(args.vg);

        // Button Body
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 2.5f);

        if (litState) {
            // PITCH/PROB (live-record, fader-focused): blue. SAVE: amber. RCL: purple.
            if (buttonId <= 1) {
                nvgFillColor(args.vg, nvgRGBA(0x34, 0x98, 0xdb, 0xff)); // Blue
            } else if (buttonId == 2) {
                nvgFillColor(args.vg, nvgRGBA(0xf1, 0xc4, 0x0f, 0xff)); // Amber
            } else {
                nvgFillColor(args.vg, nvgRGBA(155, 89, 182, 255)); // Purple
            }
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // White unlit
        }
        nvgFill(args.vg);

        // Bold white stroke to highlight active state
        if (litState) {
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
            nvgFillColor(args.vg, litState ? nvgRGBA(0xff, 0xff, 0xff, 0xff) : nvgRGBA(0x3a, 0x35, 0x2e, 0xff));

            const char* labels[4] = {"PITCH", "PROB", "SAVE", "RCL"};
            nvgTextBold(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, labels[buttonId], NULL);
        }
    }
};

/**
 * 5. Procedural Large White Knob Widget (No external SVG dependencies)
 */
// Small helper: blend a color toward white (amt > 0) or black (amt < 0),
// used to derive highlight/shadow tones for the metallic bevel from a single
// base color instead of hand-picking every shade.
static NVGcolor shadeColor(NVGcolor c, float amt) {
    if (amt >= 0.0f) {
        return nvgRGBAf(c.r + (1.0f - c.r) * amt, c.g + (1.0f - c.g) * amt, c.b + (1.0f - c.b) * amt, c.a);
    } else {
        float f = 1.0f + amt; // amt is negative here
        return nvgRGBAf(c.r * f, c.g * f, c.b * f, c.a);
    }
}

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

        float cx = box.size.x / 2.0f;
        float cy = box.size.y / 2.0f;

        // 1. Cast drop shadow -- offset down-right and soft, rather than a flat
        // halo the same size as the knob (which read as a faint outline, not a
        // shadow, since it wasn't offset from the body at all).
        NVGpaint shadowPaint = nvgRadialGradient(args.vg, cx + 1.5f, cy + 2.0f, 6.0f, 15.0f,
            nvgRGBA(0x00, 0x00, 0x00, 0x50), nvgRGBA(0x00, 0x00, 0x00, 0x00));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx + 1.5f, cy + 2.0f, 15.0f);
        nvgFillPaint(args.vg, shadowPaint);
        nvgFill(args.vg);

        // Accent ring: matches the associated FX button's color when that bus is
        // currently selected, for easy visual pairing of "this knob shapes that FX".
        if (accentGroup != 0) {
            NVGcolor accentColor;
            if (accentGroup == 2) accentColor = nvgRGBA(0x1a, 0xbc, 0x9c, 0xff);      // DELAY: Teal
            else if (accentGroup == 3) accentColor = nvgRGBA(0xff, 0x9d, 0x00, 0xff); // REVERB: Amber
            else if (accentGroup == 4) accentColor = nvgRGBA(0xe9, 0x1e, 0x63, 0xff); // FILTER: Magenta
            else accentColor = nvgRGBA(0x9b, 0x59, 0xb6, 0xff);                       // COMPRESSOR: Purple

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, 13.5f);
            nvgStrokeColor(args.vg, accentColor);
            nvgStrokeWidth(args.vg, 2.0f);
            nvgStroke(args.vg);
        }

        // 2. Metallicized trim ring -- a thin brushed-metal band around the body,
        // shaded with a diagonal gradient (light top-left to dark bottom-right)
        // rather than a single flat stroke color, so it reads as a beveled metal
        // lip rather than a painted outline.
        NVGpaint trimPaint = nvgLinearGradient(args.vg, cx - 9.0f, cy - 9.0f, cx + 9.0f, cy + 9.0f,
            nvgRGBA(0xe8, 0xe6, 0xe1, 0xff), nvgRGBA(0x6b, 0x66, 0x5e, 0xff));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 12.5f);
        nvgStrokePaint(args.vg, trimPaint);
        nvgStrokeWidth(args.vg, 1.6f);
        nvgStroke(args.vg);

        // 3. Knob body with a 3D bevel: radial gradient offset toward the
        // upper-left (simulated light source) so the body itself reads as a
        // domed cap rather than a flat-filled disc.
        NVGpaint bodyPaint = nvgRadialGradient(args.vg, cx - 4.0f, cy - 4.5f, 1.0f, 15.0f,
            shadeColor(bodyFill, 0.22f), shadeColor(bodyFill, -0.25f));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 11.0f);
        nvgFillPaint(args.vg, bodyPaint);
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, bodyStroke);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // 4. Glossy specular highlight -- small soft ellipse near the simulated
        // light source, the detail that actually sells "polished cap" rather
        // than "flat sticker".
        NVGpaint glossPaint = nvgRadialGradient(args.vg, cx - 4.0f, cy - 5.0f, 0.5f, 6.0f,
            nvgRGBA(0xff, 0xff, 0xff, active ? 0x30 : 0x60), nvgRGBA(0xff, 0xff, 0xff, 0x00));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx - 3.0f, cy - 4.0f, 6.0f);
        nvgFillPaint(args.vg, glossPaint);
        nvgFill(args.vg);

        // Indicator line rotated dynamically by parameter value, with a small
        // pointer dot at the tip for a bit of tactile detail.
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        float angle = -120.0f + value * 240.0f; // VCV standard knob angle
        float rad = angle * M_PI / 180.0f;

        float px = cx + std::sin(rad) * 10.0f;
        float py = cy - std::cos(rad) * 10.0f;

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + std::sin(rad) * 4.0f, cy - std::cos(rad) * 4.0f);
        nvgLineTo(args.vg, px, py);
        nvgStrokeColor(args.vg, indicatorColor);
        nvgStrokeWidth(args.vg, 1.6f);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, px, py, 1.1f);
        nvgFillColor(args.vg, indicatorColor);
        nvgFill(args.vg);
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
        float cx = box.size.x / 2.0f;
        float cy = box.size.y / 2.0f;
        NVGcolor bodyFill = nvgRGBA(0xfa, 0xf9, 0xf6, 0xff); // Cream-white

        // 1. Cast drop shadow, offset from the body (same technique as the big
        // knobs, scaled down).
        NVGpaint shadowPaint = nvgRadialGradient(args.vg, cx + 1.0f, cy + 1.5f, 4.0f, 11.0f,
            nvgRGBA(0x00, 0x00, 0x00, 0x38), nvgRGBA(0x00, 0x00, 0x00, 0x00));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx + 1.0f, cy + 1.5f, 11.0f);
        nvgFillPaint(args.vg, shadowPaint);
        nvgFill(args.vg);

        // 2. Metallicized trim ring
        NVGpaint trimPaint = nvgLinearGradient(args.vg, cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f,
            nvgRGBA(0xe8, 0xe6, 0xe1, 0xff), nvgRGBA(0x8a, 0x84, 0x7a, 0xff));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 9.3f);
        nvgStrokePaint(args.vg, trimPaint);
        nvgStrokeWidth(args.vg, 1.2f);
        nvgStroke(args.vg);

        // 3. Beveled body
        NVGpaint bodyPaint = nvgRadialGradient(args.vg, cx - 3.0f, cy - 3.5f, 0.5f, 11.0f,
            shadeColor(bodyFill, 0.15f), shadeColor(bodyFill, -0.12f));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 8.3f);
        nvgFillPaint(args.vg, bodyPaint);
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcb, 0xc4, 0xb5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // 4. Small specular highlight
        NVGpaint glossPaint = nvgRadialGradient(args.vg, cx - 3.0f, cy - 3.5f, 0.3f, 4.5f,
            nvgRGBA(0xff, 0xff, 0xff, 0x80), nvgRGBA(0xff, 0xff, 0xff, 0x00));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx - 2.2f, cy - 3.0f, 4.5f);
        nvgFillPaint(args.vg, glossPaint);
        nvgFill(args.vg);

        // Indicator line rotated dynamically by parameter value
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        float angle = -120.0f + value * 240.0f;
        float rad = angle * M_PI / 180.0f;

        float px = cx + std::sin(rad) * 7.5f;
        float py = cy - std::cos(rad) * 7.5f;

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + std::sin(rad) * 3.0f, cy - std::cos(rad) * 3.0f);
        nvgLineTo(args.vg, px, py);
        nvgStrokeColor(args.vg, nvgRGBA(0x60, 0x55, 0x48, 0xff));
        nvgStrokeWidth(args.vg, 1.4f);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, px, py, 0.9f);
        nvgFillColor(args.vg, nvgRGBA(0x60, 0x55, 0x48, 0xff));
        nvgFill(args.vg);
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

        // Draw selection labels Voice/Wave/Dust next to the switch positions
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 6.2f);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff)); // Black -- was low-contrast amber

            nvgTextBold(args.vg, -9.0f, 5.0f, "Voice", NULL);            // was "V" at -6
            nvgTextBold(args.vg, -9.0f, box.size.y / 2.0f, "Wave", NULL); // was "W" at -6
            nvgTextBold(args.vg, -9.0f, box.size.y - 5.0f, "Dust", NULL); // was "D" at -6
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
        SoundscapesButton::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool isActive = false;

        if (module) {
            if (label == "COMP" && module->activeFaderState == FADER_FM_SEND) isActive = true; // Enum name kept, represents Compressor bus
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
            if (label == "DELAY") {
                nvgFillColor(args.vg, nvgRGBA(0x1a, 0xbc, 0x9c, 0xff)); // Teal
            } else if (label == "REVERB") {
                nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Amber
            } else if (label == "FILTER") {
                nvgFillColor(args.vg, nvgRGBA(0xe9, 0x1e, 0x63, 0xff)); // Magenta
            } else {
                nvgFillColor(args.vg, nvgRGBA(0x9b, 0x59, 0xb6, 0xff)); // Purple (COMP)
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
 * X-Y Wildcard Transpose Pad -- drives two params at once (WILDCARD_X_PARAM/
 * WILDCARD_Y_PARAM) rather than the usual single-param ParamWidget pattern.
 * Center = both params at 0.5 = fully off. Double-click recenters both --
 * mouse-only, since there's no equivalent gesture on hardware; on MetaModule,
 * X and Y are just two separately-mappable knobs, so a patch reload/manually
 * turning both back to noon is the hardware equivalent.
 */
struct XYPadWidget : OpaqueWidget {
    Soundscapes* module = nullptr;
    Vec dragPos;
    double lastClickTime = -1.0;

    XYPadWidget() {
        box.size = Vec(50.0f, 50.0f);
    }

    void setFromLocalPos(Vec pos) {
        if (!module) return;
        float x = math::clamp(pos.x / box.size.x, 0.0f, 1.0f);
        float y = math::clamp(1.0f - (pos.y / box.size.y), 0.0f, 1.0f); // up = higher Y
        module->params[Soundscapes::WILDCARD_X_PARAM].setValue(x);
        module->params[Soundscapes::WILDCARD_Y_PARAM].setValue(y);
    }

    void onButton(const event::Button& e) override {
        if (e.action != GLFW_PRESS) return;

        if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            // Right-click: guaranteed, timing-independent recenter. Added as a
            // backup to double-click, which depends on manually-tracked timing
            // that's harder to verify works identically across platforms.
            if (module) {
                module->params[Soundscapes::WILDCARD_X_PARAM].setValue(0.5f);
                module->params[Soundscapes::WILDCARD_Y_PARAM].setValue(0.5f);
            }
            e.consume(this);
            return;
        }

        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;

        double now = system::getTime();
        if (lastClickTime > 0.0 && (now - lastClickTime) < 0.4) {
            // Double-click: recenter both axes (= off)
            if (module) {
                module->params[Soundscapes::WILDCARD_X_PARAM].setValue(0.5f);
                module->params[Soundscapes::WILDCARD_Y_PARAM].setValue(0.5f);
            }
            lastClickTime = -1.0;
        } else {
            lastClickTime = now;
            dragPos = e.pos;
            setFromLocalPos(e.pos);
        }
        e.consume(this);
    }

    void onDragMove(const event::DragMove& e) override {
        dragPos = dragPos.plus(e.mouseDelta);
        setFromLocalPos(dragPos);
    }

    void draw(const DrawArgs& args) override {
        // Background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 4.0f);
        nvgFillColor(args.vg, nvgRGBA(0x2b, 0x28, 0x24, 0xff));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(0xcb, 0xc4, 0xb5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Center crosshair marks the "off" position
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, box.size.x / 2.0f, 4.0f);
        nvgLineTo(args.vg, box.size.x / 2.0f, box.size.y - 4.0f);
        nvgMoveTo(args.vg, 4.0f, box.size.y / 2.0f);
        nvgLineTo(args.vg, box.size.x - 4.0f, box.size.y / 2.0f);
        nvgStrokeColor(args.vg, nvgRGBA(0x5c, 0x53, 0x46, 0x70));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        float x = 0.5f, y = 0.5f;
        if (module) {
            x = module->params[Soundscapes::WILDCARD_X_PARAM].getValue();
            y = module->params[Soundscapes::WILDCARD_Y_PARAM].getValue();
        }
        float px = x * box.size.x;
        float py = (1.0f - y) * box.size.y;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, px, py, 4.0f);
        nvgFillColor(args.vg, nvgRGBA(0x9b, 0x59, 0xb6, 0xff)); // Purple, matches Compressor accent
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgStrokeWidth(args.vg, 1.2f);
        nvgStroke(args.vg);
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
            nvgFontSize(args.vg, 16.0f); // was 13
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, nvgRGBA(0x11, 0x11, 0x11, 0xff)); // Dark bold
            nvgTextBold(args.vg, 232.5f, 23.0f, "SOUNDSCAPES", NULL);

            // B. Draw Module Slogan
            nvgFontSize(args.vg, 8.5f); // was 7
            nvgFillColor(args.vg, nvgRGBA(0x6d, 0x65, 0x58, 0xff)); // Cream-charcoal
            nvgTextBold(args.vg, 232.5f, 38.0f, "6-Channel Poly-Engine", NULL);

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

            // D. Output Jack Column Numbers (1 to 6), then L/R for the master outs.
            // Printed once, above the 7-seg display (between it and the jack) --
            // the display itself no longer repeats this when idle (see
            // OpaqueDisplay::draw), so there's no more duplicate second row below.
            for (int i = 0; i < 6; i++) {
                float x = SoundscapesCoords::CH_COLS[i];
                nvgFontSize(args.vg, 7.0f);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW1_JACK_Y + 16.0f, std::to_string(i + 1).c_str(), NULL);
            }
            nvgFontSize(args.vg, 7.0f);
            nvgTextBold(args.vg, SoundscapesCoords::CH_COLS[6], SoundscapesCoords::ROW1_JACK_Y + 16.0f, "L", NULL);
            nvgTextBold(args.vg, SoundscapesCoords::CH_COLS[7], SoundscapesCoords::ROW1_JACK_Y + 16.0f, "R", NULL);

            // E. Fader Numbers (1 to 6)
            for (int i = 0; i < 6; i++) {
                float x = SoundscapesCoords::GRID_COLS[i];
                nvgFontSize(args.vg, 7.0f);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW3_FADER_Y + 34.0f, std::to_string(i + 1).c_str(), NULL);
            }

            // F. Macro Knob Labels
            const char* knobLabels[6] = {"RATE", "DYNAMICS", "SPREAD", "TIMBRE", "TEXTURE", "DENSITY"};
            for (int i = 0; i < 6; i++) {
                nvgFontSize(args.vg, 7.5f);
                nvgTextBold(args.vg, SoundscapesCoords::KNOB_COLS[i], SoundscapesCoords::ROW2_KNOB_Y + 21.0f, knobLabels[i], NULL);
            }

            // G. Root & Scale Labels -- now sized to match the 6 big-knob labels
            // (7.5) instead of the old smaller 6.5, for visual consistency, and
            // positioned below their knobs same as the big knobs are.
            nvgFontSize(args.vg, 7.5f);
            nvgTextBold(args.vg, SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y + 21.0f, "ROOT", NULL);
            nvgTextBold(args.vg, SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y + 21.0f, "SCALE", NULL);

            // H. Step Numbers (unified 16-step row, physically laid out as two
            // rows of 8: row 1 = steps 1-8, row 2 = steps 9-16)
            nvgFontSize(args.vg, 6.5f);
            for (int i = 0; i < 8; i++) {
                float x = SoundscapesCoords::GRID_COLS[i];
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW4_MELODY_PAD_Y + 18.0f, std::to_string(i + 1).c_str(), NULL);
                nvgTextBold(args.vg, x, SoundscapesCoords::ROW4_CHORD_PAD_Y + 18.0f, std::to_string(i + 9).c_str(), NULL);
            }
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

        // 2x2 FX Button Group -- top-left to bottom-right: Delay, Reverb, Filter, Compressor
        FXButtonWidget* dlyBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[0], SoundscapesCoords::FX_ROWS[0]), module, Soundscapes::DELAY_PARAM);
        dlyBtn->label = "DELAY";
        addParam(dlyBtn);

        FXButtonWidget* revBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[1], SoundscapesCoords::FX_ROWS[0]), module, Soundscapes::REVERB_PARAM);
        revBtn->label = "REVERB";
        addParam(revBtn);

        FXButtonWidget* fltBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[0], SoundscapesCoords::FX_ROWS[1]), module, Soundscapes::FILTER_PARAM);
        fltBtn->label = "FILTER";
        addParam(fltBtn);

        FXButtonWidget* compBtn = createParamCentered<FXButtonWidget>(Vec(SoundscapesCoords::FX_COLS[1], SoundscapesCoords::FX_ROWS[1]), module, Soundscapes::COMPRESSOR_PARAM);
        compBtn->label = "COMP";
        addParam(compBtn);

        // 6 Large Parameter Knobs
        for (int i = 0; i < 6; i++) {
            // Physical left-to-right order: Rate, Dynamics, Spread, Timbre, Texture,
            // Density -- was Rate, Density, Timbre, Texture, Spread, Dynamics.
            // Param identities unchanged, just which column each sits in.
            static const int knobOrder[6] = {
                Soundscapes::RATE_PARAM, Soundscapes::DYNAMICS_PARAM, Soundscapes::SPREAD_PARAM,
                Soundscapes::TIMBRE_PARAM, Soundscapes::TEXTURE_PARAM, Soundscapes::DENSITY_PARAM
            };
            addParam(createParamCentered<SoundscapesKnob>(Vec(SoundscapesCoords::KNOB_COLS[i], SoundscapesCoords::ROW2_KNOB_Y), module, knobOrder[i]));
        }

        // --- IV. Row 3: Mixer Faders & Diagonal Quantizer Knobs ---
        // 6 Volume/Live-Record Faders centered over track positions (neutral cap)
        for (int i = 0; i < 6; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];
            addParam(createParamCentered<SoundscapesFader>(Vec(x, SoundscapesCoords::ROW3_FADER_Y), module, Soundscapes::FADER1_PARAM + i));
        }

        // 2 Global Faders (previously blank strips at columns 7-8): FX Return and
        // Master Level. Distinct cap colors mark them as global, not per-channel.
        FxReturnFader* fxReturnFader = createParamCentered<FxReturnFader>(Vec(SoundscapesCoords::GRID_COLS[6], SoundscapesCoords::ROW3_FADER_Y), module, Soundscapes::FX_RETURN_PARAM);
        addParam(fxReturnFader);

        MasterLevelFader* masterLevelFader = createParamCentered<MasterLevelFader>(Vec(SoundscapesCoords::GRID_COLS[7], SoundscapesCoords::ROW3_FADER_Y), module, Soundscapes::MASTER_LEVEL_PARAM);
        addParam(masterLevelFader);

        // Diagonal Large Quantizers on Columns 9 and 10
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y), module, Soundscapes::ROOT_PARAM));
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y), module, Soundscapes::SCALE_PARAM));

        // X-Y Wildcard Transpose pad -- lives in the space freed by ROOT/SCALE now
        // sharing one row instead of sitting diagonally. Centered between the two
        // quantizer columns, between the ROOT/SCALE label and the performance
        // button block below.
        {
            XYPadWidget* xyPad = new XYPadWidget();
            xyPad->module = module;
            float xyCenterX = (SoundscapesCoords::GRID_COLS[8] + SoundscapesCoords::GRID_COLS[9]) / 2.0f;
            xyPad->box.pos = Vec(xyCenterX - xyPad->box.size.x / 2.0f, 268.0f - xyPad->box.size.y / 2.0f);
            addChild(xyPad);
        }

        // --- V. Row 4: Step Sequencer Pads & Performance Block ---
        // Unified 16-step row, physically laid out as two rows of 8 (steps 1-8,
        // then 9-16). No more melody/chord split -- each pad is just step N of one
        // shared 16-step timeline; every channel has its own pitch/probability at
        // that step (see Soundscapes::stepPitch/stepProb).
        for (int i = 0; i < 8; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];

            StepPadWidget* padRow1 = createParamCentered<StepPadWidget>(Vec(x, SoundscapesCoords::ROW4_MELODY_PAD_Y), module, Soundscapes::STEP_PARAM_START + i);
            padRow1->padId = i;
            addParam(padRow1);

            StepPadWidget* padRow2 = createParamCentered<StepPadWidget>(Vec(x, SoundscapesCoords::ROW4_CHORD_PAD_Y), module, Soundscapes::STEP_PARAM_START + 8 + i);
            padRow2->padId = 8 + i;
            addParam(padRow2);
        }

        // 2x2 Utility Button Grid on Columns 9 & 10 (PITCH, PROB, SAVE, RCL)
        for (int row = 0; row < 2; row++) {
            float y = SoundscapesCoords::ROW4_BUTTON_ROWS[row];
            int btnIndex = row * 2;

            // Column 1 Button
            PerformanceButtonWidget* btn1 = createParamCentered<PerformanceButtonWidget>(Vec(SoundscapesCoords::GRID_COLS[8], y), module, Soundscapes::PITCH_PARAM + btnIndex);
            btn1->buttonId = btnIndex;
            addParam(btn1);

            // Column 2 Button
            PerformanceButtonWidget* btn2 = createParamCentered<PerformanceButtonWidget>(Vec(SoundscapesCoords::GRID_COLS[9], y), module, Soundscapes::PITCH_PARAM + btnIndex + 1);
            btn2->buttonId = btnIndex + 1;
            addParam(btn2);
        }
    }
};

// Model definition binding class implementations to unique slug
Model* modelSoundscapes = createModel<Soundscapes, SoundscapesWidget>("soundscapes-mm");
