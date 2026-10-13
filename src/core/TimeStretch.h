#pragma once

// Pitch-preserving time-stretch (WSOLA: overlap-add with similarity search).
// Done off the audio thread, once per (sample, ratio), like FL's "offline" modes
// (docs/analise/REA_rodada3.md §5).

#include <memory>

#include "core/Instruments.h"

namespace lynx
{
// `ratio` = output duration / input duration (2 = twice as long, same pitch).
std::shared_ptr<SampleData> timeStretch (const SampleData& input, double ratio);
} // namespace lynx
