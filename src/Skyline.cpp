#include "plugin.hpp"
#include "widgets/Knobs.hpp"
#include "widgets/PanelTheme.hpp"

struct TextLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    bool bold;

    TextLabel(Vec pos, Vec size, std::string text, float fontSize = 8.f,
              NVGcolor color = nvgRGB(255, 255, 255), bool bold = true) {
        box.pos = pos;
        box.size = size;
        this->text = text;
        this->fontSize = fontSize;
        this->color = color;
        this->bold = bold;
    }

    void draw(const DrawArgs& args) override {
        nvgFontSize(args.vg, fontSize);
        nvgFontFaceId(args.vg, APP->window->uiFont->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, color);
        nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
        if (bold) {
            nvgStrokeColor(args.vg, color);
            nvgStrokeWidth(args.vg, 0.3f);
            nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
        }
    }
};

struct WhiteBox : Widget {
    WhiteBox(Vec pos, Vec size) {
        box.pos = pos;
        box.size = size;
    }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, nvgRGBA(200, 200, 200, 255));
        nvgStroke(args.vg);
    }
};

struct Skyline : Module {
    int panelTheme = maybeDefaultTheme;
    float panelContrast = maybeDefaultContrast;

    enum ParamIds {
        LEVEL_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        AUDIO_INPUT,
        CV_INPUT,
        GATE_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        AUDIO_OUTPUT,
        CV_OUTPUT,
        GATE_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    Skyline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(LEVEL_PARAM, 0.f, 1.f, 0.5f, "Level");
        configInput(AUDIO_INPUT, "Audio");
        configInput(CV_INPUT, "CV");
        configInput(GATE_INPUT, "Gate");
        configOutput(AUDIO_OUTPUT, "Audio");
        configOutput(CV_OUTPUT, "CV");
        configOutput(GATE_OUTPUT, "Gate");
    }

    void process(const ProcessArgs& args) override {
        float level = params[LEVEL_PARAM].getValue();
        float audioIn = inputs[AUDIO_INPUT].getVoltage();
        outputs[AUDIO_OUTPUT].setVoltage(audioIn * level);
        float cvIn = inputs[CV_INPUT].getVoltage();
        outputs[CV_OUTPUT].setVoltage(cvIn);
        float gateIn = inputs[GATE_INPUT].getVoltage();
        outputs[GATE_OUTPUT].setVoltage(gateIn);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
        json_object_set_new(rootJ, "panelContrast", json_real(panelContrast));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* themeJ = json_object_get(rootJ, "panelTheme");
        if (themeJ) panelTheme = json_integer_value(themeJ);
        json_t* contrastJ = json_object_get(rootJ, "panelContrast");
        if (contrastJ) panelContrast = json_real_value(contrastJ);
    }
};

struct SkylineWidget : ModuleWidget {
    PanelThemeHelper panelThemeHelper;

    SkylineWidget(Skyline* module) {
    setModule(module);
    panelThemeHelper.init(this, "Skyline", module ? &module->panelContrast : nullptr);
    box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // Title
        addChild(new TextLabel(Vec(0, 1), Vec(box.size.x, 14), "maybe", 10.f, nvgRGB(255, 200, 0), false));
        addChild(new TextLabel(Vec(0, 13), Vec(box.size.x, 14), "SKYLINE", 8.f, nvgRGB(255, 200, 0), true));

        // Level knob
        addChild(new TextLabel(Vec(0, 99), Vec(box.size.x, 10), "LEVEL", 8.f, nvgRGB(255, 255, 255), true));
        addParam(createParamCentered<StandardBlackKnob>(Vec(30, 123), module, Skyline::LEVEL_PARAM));

        // Inputs
        addChild(new TextLabel(Vec(0, 176), Vec(30, 10), "AUDIO", 8.f, nvgRGB(255, 255, 255), true));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 200), module, Skyline::AUDIO_INPUT));

        addChild(new TextLabel(Vec(0, 201), Vec(30, 10), "CV", 8.f, nvgRGB(255, 255, 255), true));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 225), module, Skyline::CV_INPUT));

        addChild(new TextLabel(Vec(0, 226), Vec(30, 10), "GATE", 8.f, nvgRGB(255, 255, 255), true));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 250), module, Skyline::GATE_INPUT));

        // White output area
        addChild(new WhiteBox(Vec(0, 330), Vec(box.size.x, 50)));

        addChild(new TextLabel(Vec(0, 331), Vec(30, 10), "AUDIO", 7.f, nvgRGB(255, 133, 133), true));
        addOutput(createOutputCentered<PJ301MPort>(Vec(15, 343), module, Skyline::AUDIO_OUTPUT));

        addChild(new TextLabel(Vec(30, 331), Vec(30, 10), "CV", 7.f, nvgRGB(255, 133, 133), true));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 343), module, Skyline::CV_OUTPUT));

        addChild(new TextLabel(Vec(0, 356), Vec(30, 10), "GATE", 7.f, nvgRGB(0, 0, 0), true));
        addOutput(createOutputCentered<PJ301MPort>(Vec(15, 368), module, Skyline::GATE_OUTPUT));
    }

    void step() override {
        Skyline* module = dynamic_cast<Skyline*>(this->module);
        if (module) panelThemeHelper.step(module);
        ModuleWidget::step();
    }

    void appendContextMenu(ui::Menu* menu) override {
        Skyline* module = dynamic_cast<Skyline*>(this->module);
        if (!module) return;
        addPanelThemeMenu(menu, module);
    }
};

Model* modelSkyline = createModel<Skyline, SkylineWidget>("Skyline");
