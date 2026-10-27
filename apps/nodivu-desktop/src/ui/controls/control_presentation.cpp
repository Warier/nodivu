#include "ui/controls/control_presentation.hpp"
#include <algorithm>
#include <cmath>

namespace nodivu {
QString controlValueText(const ControlDescriptor& control, double value) {
    if (control.key == QStringLiteral("gain_db"))
        return QStringLiteral("%1 dB · %2%")
            .arg(value, 0, 'f', 1)
            .arg(100 * std::pow(10, value / 20), 0, 'f', 1);
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', control.step >= 1 ? 0 : 3)
        .arg(control.unit)
        .trimmed();
}
double sliderControlValue(const ControlDescriptor& control, int position) {
    const double value = control.minimum + (control.maximum - control.minimum) *
                                               std::clamp(position, 0, ControlSliderSteps) /
                                               ControlSliderSteps;
    return std::clamp(control.step > 0 ? std::round(value / control.step) * control.step : value,
                      control.minimum, control.maximum);
}
int sliderControlPosition(const ControlDescriptor& control, double value) {
    return control.maximum == control.minimum
               ? 0
               : qRound(ControlSliderSteps * (value - control.minimum) /
                        (control.maximum - control.minimum));
}
} // namespace nodivu
