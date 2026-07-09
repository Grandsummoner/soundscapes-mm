#include "soundscapes.hpp"

struct DisplayButton : Widget {
    Soundscapes* module;
    int channelId;

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (module) module->toggleChannelFocus(channelId);
            e.consume(this);
        }
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        bool isFocused = (module->focusedChannel == channelId);
        bool isProbMode = module->probMode;
        
        // Define color states
        NVGcolor activeColor;
        if (isProbMode) {
            activeColor = nvgRGBA(180, 50, 250, 255); // Purple (Probability)
        } else if (isFocused) {
            activeColor = nvgRGBA(50, 250, 100, 255);  // Vibrant Green (Step Editing)
        } else {
            activeColor = nvgRGBA(217, 93, 18, 255);   // Original Note Orange
        }

        // Draw digital letters
        nvgFillColor(args.vg, activeColor);
        std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/RobotoMono-Regular.ttf"));
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 12.0f);
            
            // Display note abbreviation based on step pitch
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
        
        // Match 28HP panel
        box.size = Vec(142.2f * 1.5f, 380.0f);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/soundscapes-mm.svg")));

        // Left sidebar patch inputs (mm coordinates mapping to background design)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 55.0f)), module, Soundscapes::CLK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 95.0f)), module, Soundscapes::RST_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 135.0f)), module, Soundscapes::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 175.0f)), module, Soundscapes::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 215.0f)), module, Soundscapes::VEL_INPUT));
        
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 265.0f)), module, Soundscapes::EXTIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.5f, 315.0f)), module, Soundscapes::DUCK_INPUT));

        // Top Output Ports (Jacks 1-8)
        float outStartX = 70.0f;
        float outSpacingX = 35.0f;
        for (int i = 0; i < 4; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outStartX + (i * outSpacingX), 55.0f)), module, Soundscapes::PORT_1_OUTPUT + i));
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(outStartX + (i * outSpacingX), 65.0f)), module, Soundscapes::PORT_1_LIGHT + i));
        }
        for (int i = 4; i < 8; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outStartX + 70.0f + (i * outSpacingX), 55.0f)), module, Soundscapes::PORT_1_OUTPUT + i));
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(outStartX + 70.0f + (i * outSpacingX), 65.0f)), module, Soundscapes::PORT_1_LIGHT + i));
        }

        // Clickable 7-Segment Displays
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
            dBtn->box.pos = mm2px(Vec(outStartX + 70.0f + (i * outSpacingX) - 14.0f, 93.0f));
            dBtn->box.size = mm2px(Vec(28.0f, 15.0f));
            addChild(dBtn);
        }

        // Vertical Physical Sliders
        for (int i = 0; i < 4; i++) {
            addParam(createParamCentered<VCVSlider>(mm2px(Vec(outStartX + (i * outSpacingX), 235.0f)), module, Soundscapes::FADER_1_PARAM + i));
        }
        for (int i = 4; i < 8; i++) {
            addParam(createParamCentered<VCVSlider>(mm2px(Vec(outStartX + 70.0f + (i * outSpacingX), 235.0f)), module, Soundscapes::FADER_1_PARAM + i));
        }

        // Center Quantizers
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(210.0f, 232.0f)), module, Soundscapes::ROOT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(245.0f, 232.0f)), module, Soundscapes::SCALE_PARAM));

        // Right Sidebar Buttons
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
