#pragma once

// Detecção de ataques para fatiar loops (como o Slicex/Fruity Slicer do FL).

#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// Inícios das fatias, em frações do sample (o primeiro é sempre 0). `sensitivity` 0..1:
// mais alto = mais fatias. No máximo `maxSlices`.
std::vector<float> detectSlices (const SampleData& sample, float sensitivity = 0.5f, int maxSlices = 64);
} // namespace lynx
