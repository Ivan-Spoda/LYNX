#pragma once

// Transient detection for slicing loops (like FL's Slicex/Fruity Slicer).

#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// Slice starts, as fractions of the sample (the first is always 0). `sensitivity` 0..1:
// higher = more slices. At most `maxSlices`.
std::vector<float> detectSlices (const SampleData& sample, float sensitivity = 0.5f, int maxSlices = 64);
} // namespace lynx
