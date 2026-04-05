#pragma once
#include <rack.hpp>
#include <settings.hpp>
#include "../plugin.hpp"

static constexpr float panelContrastDefault = 255.0f;
static constexpr float panelContrastMin = 160.0f;
static constexpr float panelContrastMax = 255.0f;

struct PanelContrastWidget : TransparentWidget {
    float* contrastSrc = nullptr;

    PanelContrastWidget(Vec size, float* src) {
        box.size = size;
        contrastSrc = src;
    }

    void draw(const DrawArgs& args) override {
        if (!contrastSrc) return;
        float contrast = clamp(*contrastSrc, panelContrastMin, panelContrastMax);
        if (contrast < panelContrastMax) {
            float alpha = (panelContrastMax - contrast) / panelContrastMax;
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
            nvgFillColor(args.vg, nvgRGBA(0, 0, 0, (int)(alpha * 255)));
            nvgFill(args.vg);
        }
    }
};

struct PanelThemeHelper {
    SvgPanel* sashimiPanel = nullptr;
    SvgPanel* boringPanel = nullptr;
    SvgPanel* toiletPaperPanel = nullptr;
    SvgPanel* winePanel = nullptr;
    PanelContrastWidget* contrastWidget = nullptr;

    void init(ModuleWidget* widget, const std::string& baseName, float* contrastSrc = nullptr) {
        sashimiPanel = createPanel(asset::plugin(pluginInstance, "res/" + baseName + "_Sashimi.svg"));

        auto loadPanel = [&](SvgPanel*& panel, const std::string& name) {
            std::shared_ptr<Svg> svg = Svg::load(asset::plugin(pluginInstance, "res/" + name));
            if (svg) {
                panel = new SvgPanel();
                panel->setBackground(svg);
                panel->visible = false;
                widget->addChild(panel);
            }
        };

        loadPanel(boringPanel, baseName + "_Boring.svg");
        loadPanel(toiletPaperPanel, baseName + "_ToiletPaper.svg");
        loadPanel(winePanel, baseName + "_Wine.svg");

        if (contrastSrc) {
            contrastWidget = new PanelContrastWidget(widget->box.size, contrastSrc);
            widget->addChild(contrastWidget);
        }
    }

    template<typename TModule>
    void step(TModule* module) {
        if (!module || !boringPanel || !toiletPaperPanel || !winePanel) return;
        int theme = module->panelTheme;
        int effectiveTheme;
        if (theme <= 0) {
            effectiveTheme = settings::preferDarkPanels ? 1 : 0;
        } else if (theme == 4) {
            effectiveTheme = 0;
        } else {
            effectiveTheme = theme;
        }
        boringPanel->visible = (effectiveTheme == 1);
        toiletPaperPanel->visible = (effectiveTheme == 2);
        winePanel->visible = (effectiveTheme == 3);
    }
};

struct PanelContrastQuantity : Quantity {
    float* contrastSrc = nullptr;
    static constexpr float range = panelContrastMax - panelContrastMin;

    PanelContrastQuantity(float* src) : contrastSrc(src) {}

    void setValue(float percentage) override {
        if (contrastSrc) {
            float internal = panelContrastMin + (percentage / 100.0f) * range;
            *contrastSrc = clamp(internal, panelContrastMin, panelContrastMax);
        }
    }

    float getValue() override {
        if (!contrastSrc) return getDefaultValue();
        return ((*contrastSrc - panelContrastMin) / range) * 100.0f;
    }

    float getMinValue() override { return 0.0f; }
    float getMaxValue() override { return 100.0f; }
    float getDefaultValue() override {
        return ((panelContrastDefault - panelContrastMin) / range) * 100.0f;
    }
    std::string getLabel() override { return "Panel contrast"; }
    std::string getUnit() override { return ""; }
    int getDisplayPrecision() override { return 0; }
    std::string getDisplayValueString() override {
        return std::to_string((int)std::round(getValue())) + "%";
    }
};

struct PanelContrastSlider : ui::Slider {
    PanelContrastSlider(float* contrastSrc) {
        quantity = new PanelContrastQuantity(contrastSrc);
        box.size.x = 200.0f;
    }
    ~PanelContrastSlider() {
        delete quantity;
    }
};

template<typename TModule>
inline void addPanelThemeMenu(ui::Menu* menu, TModule* module) {
    menu->addChild(new ui::MenuSeparator);
    menu->addChild(createMenuLabel("Panel Theme"));

    struct ThemeItem : ui::MenuItem {
        TModule* module;
        int theme;
        bool isAuto = false;
        void onAction(const event::Action& e) override {
            if (module) module->panelTheme = theme;
        }
        void step() override {
            if (module) {
                if (isAuto) rightText = (module->panelTheme <= 0) ? "✔" : "";
                else rightText = (module->panelTheme == theme) ? "✔" : "";
            }
            MenuItem::step();
        }
    };

    ThemeItem* sashimiItem = createMenuItem<ThemeItem>("Sashimi");
    sashimiItem->module = module;
    sashimiItem->theme = 4;
    menu->addChild(sashimiItem);

    ThemeItem* boringItem = createMenuItem<ThemeItem>("Boring");
    boringItem->module = module;
    boringItem->theme = 1;
    menu->addChild(boringItem);

    ThemeItem* toiletPaperItem = createMenuItem<ThemeItem>("Toilet Paper");
    toiletPaperItem->module = module;
    toiletPaperItem->theme = 2;
    menu->addChild(toiletPaperItem);

    ThemeItem* wineItem = createMenuItem<ThemeItem>("Wine");
    wineItem->module = module;
    wineItem->theme = 3;
    menu->addChild(wineItem);

    if (module) {
        menu->addChild(new ui::MenuSeparator);

        struct SaveThemeDefaultItem : ui::MenuItem {
            TModule* mod;
            void onAction(const event::Action& e) override {
                if (mod) { maybeDefaultTheme = mod->panelTheme; maybeSaveSettings(); }
            }
        };
        SaveThemeDefaultItem* saveThemeItem = createMenuItem<SaveThemeDefaultItem>("Save theme as default");
        saveThemeItem->mod = module;
        menu->addChild(saveThemeItem);

        struct ApplyThemeAllItem : ui::MenuItem {
            TModule* mod;
            void onAction(const event::Action& e) override {
                if (mod) maybeApplyThemeToAll(mod->panelTheme);
            }
        };
        ApplyThemeAllItem* applyThemeItem = createMenuItem<ApplyThemeAllItem>("Apply theme to all maybe modules");
        applyThemeItem->mod = module;
        menu->addChild(applyThemeItem);

        menu->addChild(new ui::MenuSeparator);
        menu->addChild(createMenuLabel("Panel Contrast"));
        menu->addChild(new PanelContrastSlider(&module->panelContrast));

        struct SaveContrastDefaultItem : ui::MenuItem {
            TModule* mod;
            void onAction(const event::Action& e) override {
                if (mod) { maybeDefaultContrast = mod->panelContrast; maybeSaveSettings(); }
            }
        };
        SaveContrastDefaultItem* saveContrastItem = createMenuItem<SaveContrastDefaultItem>("Save contrast as default");
        saveContrastItem->mod = module;
        menu->addChild(saveContrastItem);

        struct ApplyContrastAllItem : ui::MenuItem {
            TModule* mod;
            void onAction(const event::Action& e) override {
                if (mod) maybeApplyContrastToAll(mod->panelContrast);
            }
        };
        ApplyContrastAllItem* applyContrastItem = createMenuItem<ApplyContrastAllItem>("Apply contrast to all maybe modules");
        applyContrastItem->mod = module;
        menu->addChild(applyContrastItem);
    }
}
