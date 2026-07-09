#include "soundscapes.hpp"

// Deriving from OpaqueWidget ensures mouse events consume correctly on hardware
struct DisplayButton : OpaqueWidget {
    Soundscapes* module;
    int channelId;
    std::shared_ptr<Font> displayFont;

    DisplayButton() {
        displayFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/RobotoMono-Regular.ttf"));
    }

    // Overridden with VCV Rack v2 event::Button namespace
    void onButton(const event::Button& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (module) module->toggleChannelFocus(channelId);
            e.consume(this);
        }
    }

    void draw(const DrawArgs& args) override {
        // Draw smoked glass backgrounds
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGBA(20, 12, 7, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(54, 42, 33, 255));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);

        if (!module) return;

        bool isFocused = (module->focusedChannel == channelId);
        bool isProbMode = module->probMode;
        
        NVGcolor activeColor;
        if (isProbMode) {
            activeColor = nvgRGBA(180, 50, 250, 255); // Purple
        } else if (isFocused) {
            activeColor = nvgRGBA(50, 250, 100, 255);  // Green
        } else {
            activeColor = nvgRGBA(217, 93, 18, 255);   // Orange
        }

        nvgFillColor(args.vg, activeColor);
        if (displayFont) {
            nvgFontFaceId(args.vg, displayFont->handle);
            nvgFontSize(args.vg, 11.0f);
            
            float pVal = module->stepPitch[channelId][module->currentSteps[channelId]];
            std::string noteName = "C";
            if (pVal == 1.f) noteName = "d";
            if (pVal == 2.f) noteName = "E";
            if (pVal == 4.f) noteName = "G";
            if (pVal == 5.f) noteName = "A";
            
            nvgText(args.vg, 6.0f, 11.0f, noteName.c_str(), NULL);
        }
    }
};

struct SoundscapesWidget : ModuleWidget {
    SoundscapesWidget(Soundscapes* module) {
        setModule(module);
        box.size = Vec(142.2f * 1.5f, 380.0f); // 28HP
        setPanel(createPanel(asset::plugin(pluginInstance, "res/soundscapes-mm.svg")));

        // Coordinate panel layout (Refer to original coordinates)
        float outStartX = 70.0f;
        float outSpacingX = 35.0f;

        // Visual Jacks Overlay
        for (int i = 0; i < 4; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outStartX + (i * outSpacingX), 55.0f)), module, Soundscapes::PORT_1_OUTPUT + i));
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(outStartX + (i * outSpacingX), 65.0f)), module, Soundscapes::PORT_1_LIGHT + i));
        }
        for (int i = 4; i < 8; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outStartX + 70.0f + ((i-4) * outSpacingX), 55.0f)), module, Soundscapes::PORT_1_OUTPUT + i));
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(outStartX + 70.0f + ((i-4) * outSpacingX), 65.0f)), module, Soundscapes::PORT_1_LIGHT + i));
        }

        // Custom Clickable 7-Segment Displays
        for (int i = 0; i < 4; i++) {
            DisplayButton* dBtn = new DisplayButton();
            dBtn->module = module;
            dBtn->channelId = i;
            dBtn->box.pos = mm2px(Vec(outStartX + (i * outSpacingX) - 14.0f, 93.0f));
            dBtn->box.size = mm2px(Vec(28.0f, 15.0f));
            addChild(dBtn);
        }
        for (int i = 4; i < 8; i++) {
            DisplayButton* dBtn = new DisplayButton();
            dBtn->module = module;
            dBtn->channelId = i;
            dBtn->box.pos = mm2px(Vec(outStartX + 70.0f + ((i-4) * outSpacingX) - 14.0f, 93.0f));
            dBtn->box.size = mm2px(Vec(28.0f, 15.0f));
            addChild(dBtn);
        }

        // 8 Channels Sliders
        for (int i = 0; i < 4; i++) {
            addParam(createParamCentered<VCVSlider>(mm2px(Vec(outStartX + (i * outSpacingX), 235.0f)), module, Soundscapes::FADER_1_PARAM + i));
        }
        for (int i = 4; i < 8; i++) {
            addParam(createParamCentered<VCVSlider>(mm2px(Vec(outStartX + 70.0f + ((i-4) * outSpacingX), 235.0f)), module, Soundscapes::FADER_1_PARAM + i));
        }

        // Sidebars
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 55.0f)), module, Soundscapes::CLK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 95.0f)), module, Soundscapes::RST_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 135.0f)), module, Soundscapes::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 175.0f)), module, Soundscapes::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 215.0f)), module, Soundscapes::VEL_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 265.0f)), module, Soundscapes::EXTIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 315.0f)), module, Soundscapes::DUCK_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(210.0f, 232.0f)), module, Soundscapes::ROOT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(245.0f, 232.0f)), module, Soundscapes::SCALE_PARAM));

        float btnX = 390.0f;
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX, 55.0f)), module, Soundscapes::PLAY_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX, 125.0f)), module, Soundscapes::ARP_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX, 195.0f)), module, Soundscapes::CHRD_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX, 265.0f)), module, Soundscapes::SAVE_PARAM));

        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX + 18.0f, 55.0f)), module, Soundscapes::SHFT_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX + 18.0f, 125.0f)), module, Soundscapes::FRZ_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX + 18.0f, 195.0f)), module, Soundscapes::PROB_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(btnX + 18.0f, 265.0f)), module, Soundscapes::RCL_PARAM));
    }
};

// Declaring Model at the bottom of UI file guarantees successful linking
Model* modelSoundscapes = createModel<Soundscapes, SoundscapesWidget>("soundscapes-mm");
