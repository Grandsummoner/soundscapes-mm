#include "soundscapes.hpp"
#include "plugin.hpp"

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
 * 1. Custom Smoked Bronze Channel Display Widget (with centered alignment and glows)
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
            if (module) {
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

            std::string text = std::to_string(channelId + 1);
            nvgText(args.vg, box.size.x / 2.0f, box.size.y / 2.0f + 1.5f, text.c_str(), NULL);
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

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool stepActive = false;
        bool isPlayhead = false;

        if (module) {
            if (padId < 8) {
                stepActive = module->params[Soundscapes::STEP_PARAM_START + padId].getValue() > 0.5f;
                isPlayhead = (module->melodyTrack.playhead == padId) && module->isPlaying;
            } else {
                int chordStep = padId - 8;
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

        if (isPlayhead) {
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
    }
};

/**
 * 3. Procedural Slide Fader Handle
 */
struct SoundscapesFader : app::SvgSlider {
    SoundscapesFader() {
        box.size = Vec(14.0f, 56.0f); // Spans full slot range track height
    }

    void draw(const DrawArgs& args) override {
        // Draw fader track groove slot programmatically
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, box.size.x / 2.0f - 2.0f, 0.0f, 4.0f, box.size.y, 2.0f);
        nvgFillColor(args.vg, nvgRGBA(13, 12, 11, 255)); // #0d0c0b
        nvgFill(args.vg);

        // Position of cap handle relative to dynamic value travel
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
 * 4. Procedural Utility/Performance Buttons
 */
struct PerformanceButtonWidget : SoundscapesButton {
    int buttonId = 0; // 0 to 7
    std::shared_ptr<Font> font;

    PerformanceButtonWidget() {
        box.size = Vec(18.0f, 14.0f);
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        // Corrected: Evaluates momentary status inside button trigger handler (fixes uninitialized constructor variable)
        momentary = (buttonId != 0 && buttonId != 1);
        SoundscapesButton::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool litState = false;

        if (module) {
            if (buttonId == 0) litState = module->isPlaying;       // PLAY Button
            if (buttonId == 1) litState = module->shiftActive;     // SHFT Button
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
        if (litState) {
            if (buttonId == 0) {
                nvgFillColor(args.vg, nvgRGBA(0x2e, 0xcc, 0x71, 0xff)); // PLAY lit green
            } else {
                nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // SHFT lit amber
            }
        } else if (value > 0.5f) {
            nvgFillColor(args.vg, nvgRGBA(0xee, 0xee, 0xee, 0xff));     // Pressed grey
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));     // White unlit
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
            nvgFontSize(args.vg, 5.0f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, litState ? nvgRGBA(0xff, 0xff, 0xff, 0xff) : nvgRGBA(0x5c, 0x53, 0x46, 0xff));
            
            const char* labels[8] = {"PLAY", "SHFT", "ARP", "FRZ", "CHRD", "PROB", "SAVE", "RCL"};
            nvgText(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, labels[buttonId], NULL);
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
        // Drop shadow
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 13.0f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x08));
        nvgFill(args.vg);

        // Knob Body
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, 12.0f);
        nvgFillColor(args.vg, nvgRGBA(0xfa, 0xf9, 0xf6, 0xff)); // Cream-white
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcb, 0xc4, 0xb5, 0xff));
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
        nvgStrokeColor(args.vg, nvgRGBA(0x60, 0x55, 0x48, 0xff)); // Dark charcoal indicator
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
 * Now overrides click logic to guarantee precise toggle cycling.
 */
struct ModeThreeWaySwitch : app::ParamWidget {
    std::shared_ptr<Font> font;

    ModeThreeWaySwitch() {
        box.size = Vec(14.0f, 36.0f);
        font = loadRobustFont();
    }

    void onButton(const event::Button& e) override {
        ParamWidget::onButton(e);
        // Corrected: Handled mouse click toggles directly inside the Mode selector
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

            nvgText(args.vg, -6.0f, 5.0f, "V", NULL);
            nvgText(args.vg, -6.0f, box.size.y / 2.0f, "W", NULL);
            nvgText(args.vg, -6.0f, box.size.y - 5.0f, "D", NULL);
        }
    }
};

/**
 * 8. Procedural FX Selection Buttons (Now colorful when selected)
 */
struct FXButtonWidget : SoundscapesButton {
    std::string label;
    std::shared_ptr<Font> font;

    FXButtonWidget() {
        momentary = false;
        box.size = Vec(24.0f, 20.0f);
        font = loadRobustFont();
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
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Active amber glow
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
            nvgFontSize(args.vg, 5.0f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, isActive ? nvgRGBA(0xff, 0xff, 0xff, 0xff) : nvgRGBA(0x5c, 0x53, 0x46, 0xff));
            nvgText(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, label.c_str(), NULL);
        }
    }
};

/**
 * 9. Custom Layer Widget to Programmatically Render Label Layers
 * This layer is drawn on top of the SVG panel but behind active parameter controls,
 * completely solving missing faceplate labeling with zero SVG path dependancies.
 */
struct FaceplateLabels : Widget {
    std::shared_ptr<Font> font;

    FaceplateLabels() {
        font = loadRobustFont();
        box.size = Vec(420.0f, 380.0f); // Cover entire module width and height
    }

    void draw(const DrawArgs& args) override {
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, nvgRGBA(0x5c, 0x53, 0x46, 0xff)); // Clean charcoal
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            
            // A. Draw Module Main Header
            nvgFontSize(args.vg, 14.5f);
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, nvgRGBA(0x11, 0x11, 0x11, 0xff)); // Dark bold
            nvgText(args.vg, 232.5f, 25.0f, "SOUNDSCAPES", NULL);

            // B. Draw Module Slogan
            nvgFontSize(args.vg, 6.5f);
            nvgFillColor(args.vg, nvgRGBA(0x6d, 0x65, 0x58, 0xff)); // Cream-charcoal
            nvgText(args.vg, 232.5f, 36.0f, "8-Channel Poly-Engine", NULL);

            // C. Sidebar Labels (CLK, RST, etc.)
            const char* sidebarLabels[7] = {"CLK", "RST", "V/OCT", "GATE", "VEL", "EXT IN", "DUCK"};
            for (int i = 0; i < 7; i++) {
                float y = SoundscapesCoords::SIDEBAR_Y_START + (i * SoundscapesCoords::SIDEBAR_Y_SPACING);
                nvgFontSize(args.vg, 5.5f);
                if (i == 5) nvgFillColor(args.vg, nvgRGBA(0xd4, 0x7d, 0x00, 0xff)); // Orange for EXT IN
                else if (i == 6) nvgFillColor(args.vg, nvgRGBA(0xd0, 0x02, 0x1b, 0xff)); // Red for DUCK
                else nvgFillColor(args.vg, nvgRGBA(0x7d, 0x71, 0x60, 0xff));
                nvgText(args.vg, SoundscapesCoords::SIDEBAR_JACK_X, y + 14.0f, sidebarLabels[i], NULL);
            }
            nvgFillColor(args.vg, nvgRGBA(0x5c, 0x53, 0x46, 0xff)); // Reset to standard charcoal

            // D. Output Jack Column Numbers (1 to 8)
            for (int i = 0; i < 8; i++) {
                float x = SoundscapesCoords::CH_COLS[i];
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, x, SoundscapesCoords::ROW1_JACK_Y + 16.0f, std::to_string(i + 1).c_str(), NULL);
                nvgText(args.vg, x, SoundscapesCoords::ROW1_DISPLAY_Y + 28.0f, std::to_string(i + 1).c_str(), NULL);
            }

            // E. Fader Numbers (1 to 8)
            for (int i = 0; i < 8; i++) {
                float x = SoundscapesCoords::GRID_COLS[i];
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, x, SoundscapesCoords::ROW3_FADER_Y + 34.0f, std::to_string(i + 1).c_str(), NULL);
            }

            // F. Macro Knob Labels
            const char* knobLabels[6] = {"RATE", "DENSITY", "TIMBRE", "TEXTURE", "SPREAD", "DYNAMICS"};
            for (int i = 0; i < 6; i++) {
                nvgFontSize(args.vg, 5.0f);
                nvgText(args.vg, SoundscapesCoords::KNOB_COLS[i], SoundscapesCoords::ROW2_KNOB_Y + 21.0f, knobLabels[i], NULL);
            }

            // G. Root & Scale Labels
            nvgFontSize(args.vg, 5.0f);
            nvgText(args.vg, SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y + 21.0f, "ROOT", NULL);
            nvgText(args.vg, SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y + 21.0f, "SCALE", NULL);

            // H. Melody & Chord Labels
            nvgFontSize(args.vg, 5.5f);
            for (int i = 0; i < 8; i++) {
                float x = SoundscapesCoords::GRID_COLS[i];
                nvgText(args.vg, x, SoundscapesCoords::ROW4_MELODY_PAD_Y + 18.0f, std::to_string(i + 1).c_str(), NULL);
                nvgText(args.vg, x, SoundscapesCoords::ROW4_CHORD_PAD_Y + 18.0f, std::to_string(i + 9).c_str(), NULL);
            }

            // Section Labels
            nvgFontSize(args.vg, 6.0f);
            nvgText(args.vg, 198.0f, 315.0f, "MELODY", NULL);
            nvgText(args.vg, 198.0f, 363.0f, "CHORD", NULL);
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

        // Corrected: Add the programmatic label overlay directly on top of the SVG panel (fixes missing labels)
        FaceplateLabels* labelOverlay = new FaceplateLabels();
        addChild(labelOverlay);

        // --- I. Left Sidebar Inputs & LEDs ---
        for (int i = 0; i < Soundscapes::NUM_INPUTS; i++) {
            float y = SoundscapesCoords::SIDEBAR_Y_START + (i * SoundscapesCoords::SIDEBAR_Y_SPACING);
            addInput(createInputCentered<PJ301MPort>(Vec(SoundscapesCoords::SIDEBAR_JACK_X, y), module, i));
            addChild(createLightCentered<MediumLight<GreenLight>>(Vec(SoundscapesCoords::SIDEBAR_LED_X, y), module, i));
        }

        // --- II. Row 1: Outputs, LED Indicators, & Opaque Displays ---
        for (int i = 0; i < 8; i++) {
            float x = SoundscapesCoords::CH_COLS[i];
            addOutput(createOutputCentered<PJ301MPort>(Vec(x, SoundscapesCoords::ROW1_JACK_Y), module, i));
            addChild(createLightCentered<MediumLight<GreenLight>>(Vec(x, SoundscapesCoords::ROW1_LED_Y), module, i));

            // Custom Display overlays
            OpaqueDisplay* display = new OpaqueDisplay();
            display->box.pos = Vec(x - 14.0f, SoundscapesCoords::ROW1_DISPLAY_Y - 20.0f);
            display->box.size = Vec(28.0f, 40.0f);
            display->module = module;
            display->channelId = i;
            addChild(display);
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
        // 8 Volume/Send Faders centered over track positions
        for (int i = 0; i < 8; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];
            addParam(createParamCentered<SoundscapesFader>(Vec(x, SoundscapesCoords::ROW3_FADER_Y), module, Soundscapes::FADER1_PARAM + i));
        }

        // Diagonal Large Quantizers on Columns 9 and 10
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y), module, Soundscapes::ROOT_PARAM));
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y), module, Soundscapes::SCALE_PARAM));

        // --- V. Row 4: Step Sequencer Pads & Performance Block ---
        // 16 Step Pad triggers (Columns 1–8)
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
