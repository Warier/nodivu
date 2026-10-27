#pragma once
#include "contracts/node_descriptor.hpp"

namespace nodivu {
inline constexpr int ControlSliderSteps = 1000;
QString controlValueText(const ControlDescriptor& control, double value);
double sliderControlValue(const ControlDescriptor& control, int position);
int sliderControlPosition(const ControlDescriptor& control, double value);
} // namespace nodivu
