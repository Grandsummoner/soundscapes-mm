#pragma once
#include "../plugin.hpp"
#include "KnobStyles.hpp"

using namespace rack;

namespace madzine {
namespace widgets {

class BaseCustomKnob : public app::Knob {
protected:
    NVGcolor baseColor = KnobColors::BLACK_BASE;
    NVGcolor centerColor = KnobColors::BLACK_CENTER;
    NVGcolor borderColor = KnobColors::GRAY_BORDER;
    NVGcolor indicatorColor = KnobColors::WHITE_INDICATOR;
    float indicatorMargin = KnobSizes::INDICATOR_MARGIN;
    bool enableDoubleClickReset = true;
    float cvModulation = 0.0f;
    bool modulationEnabled = false;
    NVGcolor modPositiveColor = KnobColors::MOD_POSITIVE;
    NVGcolor modNegativeColor = KnobColors::MOD_NEGATIVE;
    float modIndicatorWidth = 1.5f;

public:
    BaseCustomKnob() : app::Knob() {
        box.size = Vec(KnobSizes::STANDARD, KnobSizes::STANDARD);
        speed = KnobSensitivity::SLOW;
        snap = false;
    }

    void initParamQuantity() override {
        app::Knob::initParamQuantity();
    }

    float getDisplayAngle() {
        ParamQuantity* pq = getParamQuantity();
        if (!pq) return 0.0f;
        float normalizedValue = pq->getScaledValue();
        float angle = rescale(normalizedValue, 0.0f, 1.0f,
                            KnobAngles::MIN_ANGLE, KnobAngles::MAX_ANGLE);
        return angle;
    }

    void setModulation(float normalizedMod) {
        cvModulation = clamp(normalizedMod, -1.0f, 1.0f);
    }

    void setModulationEnabled(bool enabled) {
        modulationEnabled = enabled;
    }

    bool isModulationEnabled() const {
        return modulationEnabled;
    }

    float getModulatedAngle() {
        float baseAngle = getDisplayAngle();
        float modRange = KnobAngles::MAX_ANGLE - KnobAngles::MIN_ANGLE;
        float modAngle = baseAngle + cvModulation * modRange;
        return clamp(modAngle, KnobAngles::MIN_ANGLE, KnobAngles::MAX_ANGLE);
    }

    virtual void drawKnob(const DrawArgs& args, float radius) {
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, radius, radius, radius - 1);
        nvgFillColor(args.vg, baseColor);
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, radius, radius, radius - 1);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, borderColor);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, radius, radius, radius - 4);
        nvgFillColor(args.vg, centerColor);
        nvgFill(args.vg);
    }

    virtual void drawIndicator(const DrawArgs& args, float radius, float angle) {
        float indicatorLength = radius - indicatorMargin;
        float lineX = radius + indicatorLength * std::sin(angle);
        float lineY = radius - indicatorLength * std::cos(angle);

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, radius, radius);
        nvgLineTo(args.vg, lineX, lineY);
        nvgStrokeWidth(args.vg, 2.0f);
        nvgStrokeColor(args.vg, indicatorColor);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, lineX, lineY, 2.0f);
        nvgFillColor(args.vg, indicatorColor);
        nvgFill(args.vg);
    }

    virtual void drawModulationIndicator(const DrawArgs& args, float radius, float modAngle) {
        if (!modulationEnabled || cvModulation == 0.0f) return;

        float indicatorLength = radius - indicatorMargin - 1.0f;
        float lineX = radius + indicatorLength * std::sin(modAngle);
        float lineY = radius - indicatorLength * std::cos(modAngle);

        NVGcolor modColor = (cvModulation > 0.0f) ? modPositiveColor : modNegativeColor;

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, radius, radius);
        nvgLineTo(args.vg, lineX, lineY);
        nvgStrokeWidth(args.vg, modIndicatorWidth);
        nvgStrokeColor(args.vg, modColor);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, lineX, lineY, 1.5f);
        nvgFillColor(args.vg, modColor);
        nvgFill(args.vg);
    }

    virtual void drawMappingIndicator(const DrawArgs& args) {
        if (!module || paramId < 0) return;
        engine::ParamHandle* paramHandle = APP->engine->getParamHandle(module->id, paramId);
        if (!paramHandle) return;
        if (paramHandle->color.a <= 0.f) return;

        float indicatorRadius = std::min(box.size.x, box.size.y) * 0.08f;
        indicatorRadius = clamp(indicatorRadius, 2.f, 3.5f);
        float x = box.size.x - indicatorRadius - 2.f;
        float y = box.size.y - indicatorRadius - 2.f;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, x, y, indicatorRadius);
        nvgFillColor(args.vg, paramHandle->color);
        nvgFill(args.vg);
    }

    void draw(const DrawArgs& args) override {
        float radius = box.size.x / 2.0f;
        float baseAngle = getDisplayAngle();
        drawKnob(args, radius);
        if (modulationEnabled && cvModulation != 0.0f) {
            float modAngle = getModulatedAngle();
            drawModulationIndicator(args, radius, modAngle);
        }
        drawIndicator(args, radius, baseAngle);
        drawMappingIndicator(args);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        app::Knob::drawLayer(args, layer);
    }

    void onDoubleClick(const event::DoubleClick& e) override {
        if (enableDoubleClickReset) {
            ParamQuantity* pq = getParamQuantity();
            if (pq) {
                pq->setValue(pq->getDefaultValue());
                e.consume(this);
                return;
            }
        }
        app::Knob::onDoubleClick(e);
    }
};

class BaseSnapKnob : public BaseCustomKnob {
public:
    BaseSnapKnob() : BaseCustomKnob() {
        snap = true;
    }
};

class BaseHiddenKnob : public BaseCustomKnob {
public:
    BaseHiddenKnob() : BaseCustomKnob() {
        box.size = Vec(1, 1);
    }
    void draw(const DrawArgs& args) override {}
};

} // namespace widgets
} // namespace madzine
