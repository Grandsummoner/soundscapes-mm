#include "soundscapes.hpp"
#include "plugin.hpp"

/**
 * 1. Custom Smoked Bronze Channel Display Widget
 */
struct OpaqueDisplay : Widget {
    Soundscapes* module = nullptr;
    int channelId = 0; // Range 0 - 7
    std::shared_ptr<Font> font;

    OpaqueDisplay() {
        font = APP->window->loadFont(asset::plugin(pluginInstance, "res/RobotoMono-Regular.ttf"));
    }

    void onButton(const ButtonEvent& e) override {
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
        bool shouldFlash = isFocused && module->displayFlashState;

        // Skip drawing text during flash-off states to create a pulsing visual effect
        if (shouldFlash) return;

        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 21.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        // Warm amber-orange glow: #ff9d00
        nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xdf));

        // Display current state (e.g. channel number, or parameter level)
        std::string text = std::to_string(channelId + 1);
        nvgText(args.vg, box.size.x / 2.0f, box.size.y / 2.0f, text.c_str(), NULL);
    }
};

/**
 * 2. Procedural Step Sequencer Pad Widget
 */
struct StepPadWidget : SvgSwitch {
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
                stepActive = module->melodyTrack.steps[padId].active;
                isPlayhead = (module->melodyTrack.playhead == padId) && module->isPlaying;
            } else {
                int chordStep = padId - 8;
                stepActive = module->chordTrack.steps[chordStep].active;
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
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // Programmed step amber glow
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // Clean white unlit pad
        }
        nvgFill(args.vg);

        // Bezel outline
        nvgStrokeColor(args.vg, nvgRGBA(0xd5, 0xcf, 0xc5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
    }
};

/**
 * 3. Procedural Slide Fader Handle
 */
struct SoundscapesFader : SvgSlider {
    SoundscapesFader() {
        box.size = Vec(14.0f, 20.0f);
    }

    void draw(const DrawArgs& args) override {
        // Draw fader track cap procedural body
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 2.0f);
        nvgFillColor(args.vg, nvgRGBA(0xfa, 0xf9, 0xf6, 0xff)); // Silver-cream
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcc, 0xc4, 0xb5, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Center black indicator line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 2.0f, box.size.y / 2.0f);
        nvgLineTo(args.vg, box.size.x - 2.0f, box.size.y / 2.0f);
        nvgStrokeColor(args.vg, nvgRGBA(0x1a, 0x1a, 0x1a, 0xff));
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStroke(args.vg);
    }
};

/**
 * 4. Procedural Utility/Performance Buttons
 */
struct PerformanceButtonWidget : SvgSwitch {
    int buttonId = 0; // 0 to 7

    PerformanceButtonWidget() {
        momentary = true;
        box.size = Vec(18.0f, 14.0f);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool litState = false;

        if (module) {
            if (buttonId == 0) litState = module->isPlaying;       // PLAY Button
            if (buttonId == 1) litState = module->shiftActive;     // SHFT Button
        }

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 2.5f);

        if (litState) {
            if (buttonId == 0) {
                nvgFillColor(args.vg, nvgRGBA(0x2e, 0xcc, 0x71, 0xff)); // PLAY lit green
            } else {
                nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff)); // SHFT lit amber
            }
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));     // White unlit
        }
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcc, 0xc4, 0xb6, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
    }
};

/**
 * 5. Procedural Large White Knob Widget (No external SVG dependencies)
 */
struct SoundscapesKnob : Knob {
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

        // Indicator line rotated dynamically by parameter value (C++11 compatible)
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        float angle = -120.0f + value * 240.0f; // VCV standard knob angle: -120 to +120 degrees
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
struct SoundscapesSmallKnob : Knob {
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
 * 7. Procedural MODE Selection Window Param Trigger
 */
struct ModeButtonWidget : ParamWidget {
    ModeButtonWidget() {
        box.size = Vec(24.0f, 40.0f); // Spans the exact mode bronze background screen
    }

    void draw(const DrawArgs& args) override {
        // Transparent parameter sensor overlay; highlights slightly when clicked (C++11 compatible)
        float value = getParamQuantity() ? getParamQuantity()->getValue() : 0.0f;
        if (value > 0.5f) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 0.0f, 0.0f, box.size.x, box.size.y, 3.0f);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x1c)); // Soft screen press highlight
            nvgFill(args.vg);
        }
    }
};

/**
 * 8. Procedural FX Selection Buttons
 */
struct FXButtonWidget : ParamWidget {
    std::string label;

    FXButtonWidget() {
        box.size = Vec(24.0f, 20.0f);
    }

    void draw(const DrawArgs& args) override {
        Soundscapes* module = dynamic_cast<Soundscapes*>(this->module);
        bool isActive = false;

        if (module) {
            // Evaluates active fader mapping mode to render state-led glows (C++11 compatible)
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
            nvgFillColor(args.vg, nvgRGBA(0x34, 0x98, 0xdb, 0xff)); // Active blue glow
        } else if (value > 0.5f) {
            nvgFillColor(args.vg, nvgRGBA(0xee, 0xee, 0xee, 0xff)); // Pressed grey
        } else {
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff)); // Idle white
        }
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(0xcc, 0xc4, 0xb6, 0xff));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        // Centered Button Label text drawn using verified monospaced font
        std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/RobotoMono-Regular.ttf"));
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
 * 9. Master ModuleWidget Panel Setup
 */
struct SoundscapesWidget : ModuleWidget {
    SoundscapesWidget(Soundscapes* module) {
        setModule(module);
        
        // Load the finalized symmetrical vector faceplate panel
        setPanel(createPanel(asset::plugin(pluginInstance, "res/soundscapes-mm.svg")));

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

        // --- III. Row 2: Centralized Synth Deck (Using procedurals) ---
        // Mode Button mapping over the bronze display box
        addParam(createParamCentered<ModeButtonWidget>(Vec(SoundscapesCoords::MODE_X, SoundscapesCoords::MODE_Y), module, Soundscapes::MODE_PARAM));

        // 2x2 FX Button Group (Using procedurals)
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
        // 8 Volume/Send Faders grouped together (Columns 1–8)
        for (int i = 0; i < 8; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];
            addParam(createParam<SoundscapesFader>(Vec(x - 7.0f, SoundscapesCoords::ROW3_FADER_Y - 23.0f), module, Soundscapes::FADER1_PARAM + i));
        }

        // Diagonal Large Quantizers on Columns 9 and 10 (Using procedurals)
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[8], SoundscapesCoords::ROOT_Y), module, Soundscapes::ROOT_PARAM));
        addParam(createParamCentered<SoundscapesSmallKnob>(Vec(SoundscapesCoords::GRID_COLS[9], SoundscapesCoords::SCALE_Y), module, Soundscapes::SCALE_PARAM));

        // --- V. Row 4: Step Sequencer Pads & Performance Block ---
        // 16 Step Pad triggers (Columns 1–8)
        for (int i = 0; i < 8; i++) {
            float x = SoundscapesCoords::GRID_COLS[i];
            
            // Melody step button (Row 1)
            StepPadWidget* melPad = createParam<StepPadWidget>(Vec(x - 12.0f, SoundscapesCoords::ROW4_MELODY_PAD_Y - 10.0f), module, Soundscapes::STEP_PARAM_START + i);
            melPad->padId = i;
            addParam(melPad);

            // Chord step button (Row 2)
            StepPadWidget* chdPad = createParam<StepPadWidget>(Vec(x - 12.0f, SoundscapesCoords::ROW4_CHORD_PAD_Y - 10.0f), module, Soundscapes::STEP_PARAM_START + 8 + i);
            chdPad->padId = 8 + i;
            addParam(chdPad);
        }

        // 2x4 Utility Button Grid on Columns 9 & 10 (Typo 'i' corrected to 'row')
        for (int row = 0; row < 4; row++) {
            float y = SoundscapesCoords::ROW4_BUTTON_ROWS[row];
            int btnIndex = row * 2;

            // Column 1 Button
            PerformanceButtonWidget* btn1 = createParam<PerformanceButtonWidget>(Vec(SoundscapesCoords::GRID_COLS[8] - 9.0f, y - 7.0f), module, Soundscapes::PLAY_PARAM + btnIndex);
            btn1->buttonId = btnIndex;
            addParam(btn1);

            // Column 2 Button
            PerformanceButtonWidget* btn2 = createParam<PerformanceButtonWidget>(Vec(SoundscapesCoords::GRID_COLS[9] - 9.0f, y - 7.0f), module, Soundscapes::PLAY_PARAM + btnIndex + 1);
            btn2->buttonId = btnIndex + 1;
            addParam(btn2);
        }
    }
};

// Model definition binding class implementations to unique slug
Model* modelSoundscapes = createModel<Soundscapes, SoundscapesWidget>("soundscapes-mm");
