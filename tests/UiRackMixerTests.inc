// UI-C (Channel Rack / Mixer polish) tests. Included at the end of tests.cpp.

#include "core/MeterBallistics.h"

TEST (meter_peak_hold)
{
    lynx::meters::PeakHold m;
    m.tick (0.5f, 0.86f, 10);
    CHECK (m.level == 0.5f && m.peak == 0.5f && ! m.clipped);

    // the bar falls at once, the hold line stays for holdTicks
    for (int i = 0; i < 10; ++i)
        m.tick (0.0f, 0.86f, 10);
    CHECK (m.level < 0.5f * 0.3f);
    CHECK (m.peak == 0.5f);

    // then it falls, never below the bar
    m.tick (0.0f, 0.86f, 10, 0.5f);
    CHECK (m.peak < 0.5f && m.peak >= m.level);
    for (int i = 0; i < 200; ++i)
        m.tick (0.0f, 0.86f, 10, 0.5f);
    CHECK (m.peak == 0.0f && m.level == 0.0f);

    // a louder block restarts the hold
    m.tick (0.2f, 0.86f, 3);
    m.tick (0.1f, 0.86f, 3);
    CHECK (m.peak == 0.2f && m.holdLeft == 2);

    // clip latches until reset
    m.tick (1.2f);
    for (int i = 0; i < 100; ++i)
        m.tick (0.0f);
    CHECK (m.clipped);
    m.resetClip();
    CHECK (! m.clipped && m.peak == m.level);

    // negative input is treated as silence
    lynx::meters::PeakHold n;
    n.tick (-1.0f);
    CHECK (n.level == 0.0f && n.peak == 0.0f);
}
