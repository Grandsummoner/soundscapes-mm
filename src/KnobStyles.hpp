#pragma once
#include <rack.hpp>

namespace madzine {
namespace widgets {

namespace KnobColors {
    const NVGcolor BLACK_BASE = nvgRGB(30, 30, 30);
    const NVGcolor BLACK_CENTER = nvgRGB(50, 50, 50);
    const NVGcolor GRAY_BORDER = nvgRGB(100, 100, 100);
    const NVGcolor WHITE_INDICATOR = nvgRGB(255, 255, 255);
    const NVGcolor WHITE_BASE = nvgRGB(255, 255, 255);
    const NVGcolor GRAY_BASE = nvgRGB(128, 128, 128);
    const NVGcolor PINK_BASE = nvgRGB(255, 192, 203);
    const NVGcolor TECHNO_GREEN = nvgRGB(0, 255, 100);
    const NVGcolor MOD_POSITIVE = nvgRGBA(0, 200, 255, 150);
    const NVGcolor MOD_NEGATIVE = nvgRGBA(255, 150, 0, 150);
}

namespace KnobSizes {
    const float STANDARD = 38.0f;
    const float TECHNO_STANDARD = 45.0f;
    const float LARGE = 46.0f;
    const float LARGE_WHITE = 37.0f;
    const float SMALL = 28.0f;
    const float SMALL_GRAY = 21.0f;
    const float MICRO = 20.0f;
    const float SNAP_STANDARD = 26.0f;
    const float SMALL_TECHNO = 15.0f;
    const float INDICATOR_MARGIN = 8.0f;
}

namespace KnobAngles {
    const float MIN_ANGLE = -0.75f * M_PI;
    const float MAX_ANGLE = 0.75f * M_PI;
}

namespace KnobSensitivity {
    const float VERY_SLOW = 1.0f;
    const float SLOW = 1.0f;
    const float NORMAL = 1.0f;
    const float FAST = 1.0f;
    const float VERY_FAST = 1.0f;
}

namespace LabelOffsets {
    const float LARGE_BLACK_KNOB = -25.0f;
    const float LARGE_WHITE_KNOB = -29.0f;
    const float STANDARD_BLACK_KNOB = -23.0f;
    const float SMALL_WHITE_KNOB = -26.0f;
    const float SMALL_BLACK_KNOB = -15.0f;
    const float SMALL_GRAY_KNOB = -15.0f;
    const float MICROTUNE_KNOB = -15.0f;
}

namespace SnapKnobParams {
    const float DEFAULT_THRESHOLD = 10.0f;
    const float FINE_THRESHOLD = 5.0f;
    const float COARSE_THRESHOLD = 20.0f;
}

} // namespace widgets
} // namespace madzine
