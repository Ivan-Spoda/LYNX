#include "core/PluginLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "core/Effects.h"
#include "core/MaximusFx.h"

// (the i18n test scans the sources for TR_NOOP: captions below are translated by the editor)
#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::plugui
{
namespace
{
// --- builders (positions in FL's window pixels, body origin) -----------------------------------------

LayoutItem section (float x, float y, float w, float h, const char* title, std::uint16_t style = 0)
{
    LayoutItem i;
    i.type = Item::section;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.label = title;
    i.style = style;
    return i;
}

// a knob centred at (cx, cy), `d` wide, its caption centred at `captionY`
LayoutItem knob (float cx, float cy, float d, const char* param, const char* caption, float captionY, std::uint16_t style = 0, int steps = 0)
{
    LayoutItem i;
    i.type = Item::knob;
    i.x = cx - d * 0.5f;
    i.y = cy - d * 0.5f - ((style & valueAbove) != 0 ? 16.0f : 0.0f);
    i.w = d;
    i.h = (captionY + 6.0f) - i.y;
    if ((style & noCaption) != 0)
        i.h = d + ((style & valueAbove) != 0 ? 16.0f : 0.0f);
    i.param = param;
    i.label = caption;
    i.style = style;
    i.steps = steps;
    return i;
}

LayoutItem label (float x, float y, float w, float h, const char* text, std::uint16_t style = 0)
{
    LayoutItem i;
    i.type = Item::label;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.label = text;
    i.style = style;
    return i;
}

LayoutItem value (float x, float y, float w, float h, const char* param, std::uint16_t style = alignLeft)
{
    LayoutItem i;
    i.type = Item::value;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.style = style;
    return i;
}

LayoutItem slider (Item type, float x, float y, float w, float h, const char* param, const char* caption = nullptr, std::uint16_t style = 0)
{
    LayoutItem i;
    i.type = type;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.style = style;
    return i;
}

LayoutItem radio (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {})
{
    LayoutItem i;
    i.type = Item::radio;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.options = options;
    i.values = std::move (values);
    return i;
}

LayoutItem tabs (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {}, std::uint16_t style = 0)
{
    LayoutItem i;
    i.type = Item::tabs;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.options = options;
    i.values = std::move (values);
    i.style = style;
    return i;
}

LayoutItem pageTabs (float x, float y, float w, float h, const char* options)
{
    auto i = tabs (x, y, w, h, nullptr, options, {}, plugui::pageTabs);
    return i;
}

LayoutItem toggle (float x, float y, float w, float h, const char* param, const char* caption, std::uint16_t style = 0)
{
    LayoutItem i;
    i.style = style;
    i.type = Item::toggle;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    return i;
}

LayoutItem stepper (float x, float y, float w, float h, const char* param, const char* caption, int steps)
{
    LayoutItem i;
    i.type = Item::stepper;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.steps = steps;
    return i;
}

LayoutItem display (float x, float y, float w, float h, const char* id, const char* param = nullptr, const char* param2 = nullptr)
{
    LayoutItem i;
    i.type = Item::display;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.display = id;
    i.param = param;
    i.param2 = param2;
    return i;
}

LayoutItem onPage (LayoutItem i, int page)
{
    i.page = page;
    return i;
}

// shown only while `param` is within [lo, hi] (two conditions at most)
LayoutItem when (LayoutItem i, const char* param, float lo, float hi)
{
    if (i.whenParam == nullptr)
    {
        i.whenParam = param;
        i.whenMin = lo;
        i.whenMax = hi;
    }
    else
    {
        i.when2Param = param;
        i.when2Min = lo;
        i.when2Max = hi;
    }
    return i;
}

// --- FL's generic "Fruity" editor: cells of 160 × 45 in three columns, 50 px apart ---------------------
// (knob at the left, the caption on a plate, the value or a slider under it)
enum class Cell { value, slider, radio };

void fruityCell (std::vector<LayoutItem>& items, int column, int row, const char* param, const char* caption, Cell kind = Cell::value,
                 int steps = 0, const char* options = nullptr)
{
    const auto x = 6.0f + 165.0f * (float) column, y = 5.0f + 50.0f * (float) row;
    items.push_back (section (x, y, 160.0f, 45.0f, nullptr, boxed));

    if (kind == Cell::radio) // half cell: plate + Off / On
        return;

    items.push_back (knob (x + 25.0f, y + 22.5f, 38.0f, param, caption, 0.0f, noCaption, steps));
    items.push_back (label (x + 54.0f, y + 3.0f, 102.0f, 15.0f, caption, boxed));
    if (kind == Cell::slider)
        items.push_back (slider (Item::hslider, x + 54.0f, y + 25.0f, 102.0f, 16.0f, param, nullptr, noCaption));
    else
        items.push_back (value (x + 57.0f, y + 23.0f, 99.0f, 18.0f, param));
    (void) options;
}

// a half-width cell with a plate and an Off / On switch (Fruity Filter's X2 / CENTER)
void fruityHalfCell (std::vector<LayoutItem>& items, int column, int row, int half, const char* param, const char* caption)
{
    const auto x = 6.0f + 165.0f * (float) column + 82.5f * (float) half, y = 5.0f + 50.0f * (float) row;
    items.push_back (section (x, y, 77.5f, 45.0f, nullptr, boxed));
    items.push_back (label (x + 18.0f, y + 3.0f, 56.0f, 15.0f, caption, boxed));
    items.push_back (radio (x + 4.0f, y + 19.0f, 44.0f, 24.0f, param, TR_NOOP ("Off|On")));
}

Layout fruityGrid (const char* fl, int rows, int columns = 3)
{
    Layout l;
    l.fl = fl;
    l.width = 6 + 165 * columns;
    l.height = 50 * rows + 5;
    return l;
}

// --- the layouts ------------------------------------------------------------------------------------

Layout fruityLimiter()
{
    // REA/FL capture: display on top (with its view strip at the left), LOUDNESS / ENVELOPE / NOISE GATE headers,
    // the LIMIT | COMP page switch at the bottom of the middle panel
    Layout l;
    l.fl = "Fruity Limiter";
    l.width = 633;
    l.height = 306;
    l.pages = { TR_NOOP ("Limit"), TR_NOOP ("Comp") };
    auto& v = l.items;
    v.push_back (display (7, 2, 623, 158, "limiter"));
    v.push_back (section (2, 161, 629, 143, nullptr, boxed));
    v.push_back (section (32, 161, 255, 16, TR_NOOP ("Loudness")));
    v.push_back (section (288, 161, 176, 16, TR_NOOP ("Envelope")));
    v.push_back (section (465, 161, 166, 16, TR_NOOP ("Noise gate")));
    v.push_back (section (117, 180, 345, 120, nullptr, boxed | raised));
    v.push_back (knob (34, 205, 44, "Gain", TR_NOOP ("Gain"), 240));
    v.push_back (knob (88, 205, 44, "Saturation", TR_NOOP ("Sat"), 240));
    v.push_back (pageTabs (119, 277, 82, 20, TR_NOOP ("Limit|Comp")));
    v.push_back (knob (492, 205, 44, "Gate release", TR_NOOP ("Rel"), 240));
    v.push_back (knob (546, 205, 44, "Gate gain", TR_NOOP ("Gain"), 240));
    v.push_back (knob (600, 205, 44, "Gate threshold", TR_NOOP ("Thres"), 240));

    // LIMIT page
    v.push_back (onPage (knob (148, 205, 44, "Ceiling", TR_NOOP ("Ceil"), 240), 0));
    v.push_back (onPage (knob (316, 205, 44, "Attack", TR_NOOP ("Att"), 240), 0));
    v.push_back (onPage (knob (372, 205, 44, "Release", TR_NOOP ("Rel"), 240), 0));
    v.push_back (onPage (knob (432, 205, 44, "Sustain", TR_NOOP ("Sustain"), 240), 0));
    v.push_back (onPage (stepper (302, 259, 28, 30, "Attack curve", TR_NOOP ("Curve"), 7), 0));
    v.push_back (onPage (stepper (358, 259, 28, 30, "Release curve", TR_NOOP ("Curve"), 7), 0));

    l.menuSwitches = { "Exact ceiling" };

    // COMP page
    v.push_back (onPage (knob (148, 205, 44, "Comp. threshold", TR_NOOP ("Thres"), 240), 1));
    v.push_back (onPage (knob (202, 205, 44, "Knee", TR_NOOP ("Knee"), 240, bipolar), 1));
    v.push_back (onPage (knob (256, 205, 44, "Ratio", TR_NOOP ("Ratio"), 240, bipolar), 1));
    v.push_back (onPage (knob (316, 205, 44, "Comp. attack", TR_NOOP ("Att"), 240), 1));
    v.push_back (onPage (knob (372, 205, 44, "Comp. release", TR_NOOP ("Rel"), 240), 1));
    // FL captions the compressor's detector window SUSTAIN too (the limiter's is its peak window)
    v.push_back (onPage (knob (432, 205, 44, "RMS window", TR_NOOP ("Sustain"), 240), 1));
    v.push_back (onPage (stepper (302, 259, 28, 30, "Comp. curve", TR_NOOP ("Curve"), 7), 1));
    return l;
}

Layout fruityCompressor()
{
    auto l = fruityGrid ("Fruity Compressor", 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Threshold", TR_NOOP ("Threshold"));
    fruityCell (v, 1, 0, "Ratio", TR_NOOP ("Ratio"));
    fruityCell (v, 2, 0, "Gain", TR_NOOP ("Gain"));
    fruityCell (v, 0, 1, "Attack", TR_NOOP ("Attack"));
    fruityCell (v, 1, 1, "Release", TR_NOOP ("Release"));
    fruityCell (v, 2, 1, "Type", TR_NOOP ("Type"), Cell::value, 7);
    l.menuSwitches = { "Look-ahead" };
    return l;
}

Layout fruityFilter()
{
    auto l = fruityGrid ("Fruity Filter", 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Cutoff", TR_NOOP ("Cutoff freq"), Cell::slider);
    fruityCell (v, 1, 0, "Resonance", TR_NOOP ("Resonance"), Cell::slider);
    fruityCell (v, 2, 0, "Low pass", TR_NOOP ("Low pass"), Cell::slider);
    fruityCell (v, 0, 1, "Band pass", TR_NOOP ("Band pass"), Cell::slider);
    fruityCell (v, 1, 1, "High pass", TR_NOOP ("High pass"), Cell::slider);
    fruityHalfCell (v, 2, 1, 0, "x2", TR_NOOP ("X 2"));
    fruityHalfCell (v, 2, 1, 1, "Center", TR_NOOP ("Center"));
    return l;
}

Layout fruityChorus()
{
    auto l = fruityGrid ("Fruity Chorus", 4);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Delay", TR_NOOP ("Delay"));
    fruityCell (v, 1, 0, "Depth", TR_NOOP ("Depth"));
    fruityCell (v, 2, 0, "Stereo", TR_NOOP ("Stereo"));
    fruityCell (v, 0, 1, "LFO 1 freq", TR_NOOP ("LFO 1 freq"));
    fruityCell (v, 1, 1, "LFO 2 freq", TR_NOOP ("LFO 2 freq"));
    fruityCell (v, 2, 1, "LFO 3 freq", TR_NOOP ("LFO 3 freq"));
    fruityCell (v, 0, 2, "LFO 1 wave", TR_NOOP ("LFO 1 wave"));
    fruityCell (v, 1, 2, "LFO 2 wave", TR_NOOP ("LFO 2 wave"));
    fruityCell (v, 2, 2, "LFO 3 wave", TR_NOOP ("LFO 3 wave"));
    fruityCell (v, 0, 3, "Cross type", TR_NOOP ("Cross type"));
    fruityCell (v, 1, 3, "Cross cutoff", TR_NOOP ("Cross cutoff"));
    fruityCell (v, 2, 3, "Wet only", TR_NOOP ("Wet only"));
    return l;
}

Layout fruityFlanger()
{
    auto l = fruityGrid ("Fruity Flanger", 4);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Delay", TR_NOOP ("Delay"));
    fruityCell (v, 1, 0, "Depth", TR_NOOP ("Depth"));
    fruityCell (v, 2, 0, "Rate", TR_NOOP ("Rate"));
    fruityCell (v, 0, 1, "Phase", TR_NOOP ("Phase"));
    fruityCell (v, 1, 1, "Damp", TR_NOOP ("Damp"));
    fruityCell (v, 2, 1, "Shape", TR_NOOP ("Shape"));
    fruityCell (v, 0, 2, "Feed", TR_NOOP ("Feed"));
    fruityCell (v, 1, 2, "Invert feed", TR_NOOP ("Inv feedback"), Cell::value, 1);
    fruityCell (v, 2, 2, "Invert wet", TR_NOOP ("Inv wet"), Cell::value, 1);
    fruityCell (v, 0, 3, "Dry", TR_NOOP ("Dry"));
    fruityCell (v, 1, 3, "Wet", TR_NOOP ("Wet"));
    fruityCell (v, 2, 3, "Cross", TR_NOOP ("Cross"));
    return l;
}

Layout fruityFastDist()
{
    // four knobs and the A/B switch on the left panel, the transfer curve on the right
    Layout l;
    l.fl = "Fruity Fast Dist";
    l.width = 386;
    l.height = 122;
    auto& v = l.items;
    v.push_back (section (0, 0, 262, 96, nullptr, boxed));
    v.push_back (knob (34, 42, 40, "Pre amp", TR_NOOP ("Pre"), 75));
    v.push_back (knob (88, 42, 40, "Threshold", TR_NOOP ("Thresh"), 75));
    v.push_back (radio (122, 30, 30, 26, "Type", "A|B"));
    v.push_back (knob (179, 42, 40, "Mix", TR_NOOP ("Mix"), 75));
    v.push_back (knob (233, 42, 40, "Post gain", TR_NOOP ("Post"), 75));
    v.push_back (label (0, 99, 258, 20, TR_NOOP ("Fast dist"), dim));
    v.push_back (display (264, 0, 122, 122, "clip", "Threshold"));
    return l;
}

Layout stereoEnhancer()
{
    // STEREO SEP | phase offset (pre/post, knob, invert) | pan and volume
    Layout l;
    l.fl = "Fruity Stereo Enhancer";
    l.width = 405;
    l.height = 116;
    auto& v = l.items;
    v.push_back (section (0, 0, 82, 116, nullptr, boxed));
    v.push_back (label (2, 14, 78, 14, TR_NOOP ("Stereo sep")));
    v.push_back (knob (44, 62, 48, "Separation", nullptr, 0, noCaption | bipolar));
    v.push_back (label (14, 92, 54, 14, "+          -", dim));
    v.push_back (section (83, 0, 190, 116, nullptr, boxed));
    v.push_back (radio (93, 20, 50, 32, "Effects post", TR_NOOP ("Pre|Post")));
    v.push_back (knob (178, 57, 48, "Phase offset", nullptr, 0, noCaption | bipolar));
    v.push_back (label (152, 86, 52, 14, "L         R", dim));
    v.push_back (radio (223, 20, 50, 48, "Invert", TR_NOOP ("None|Left|Right")));
    v.push_back (label (88, 96, 100, 18, TR_NOOP ("Phase offset"), dim | alignLeft));
    v.push_back (label (222, 96, 50, 18, TR_NOOP ("Invert"), dim | alignLeft));
    v.push_back (section (274, 0, 131, 116, nullptr, boxed));
    v.push_back (label (276, 4, 127, 16, TR_NOOP ("Stereo enhancer"), dim));
    v.push_back (knob (312, 77, 46, "Pan", TR_NOOP ("Pan"), 0, bipolar | noCaption));
    v.push_back (label (290, 38, 44, 14, TR_NOOP ("Pan")));
    v.push_back (knob (374, 77, 46, "Volume", nullptr, 0, noCaption));
    v.push_back (label (352, 38, 44, 14, TR_NOOP ("Vol")));
    return l;
}

Layout fruityDelay3()
{
    Layout l;
    l.fl = "Fruity Delay 3";
    l.width = 752;
    l.height = 220;
    auto& v = l.items;
    v.push_back (section (2, 2, 58, 216, TR_NOOP ("Input")));
    v.push_back (slider (Item::vslider, 17, 26, 30, 160, "Send", TR_NOOP ("Wet")));

    v.push_back (section (62, 2, 193, 110, TR_NOOP ("Delay time")));
    v.push_back (knob (95, 58, 56, "Time", TR_NOOP ("Time"), 104, big));
    v.push_back (toggle (142, 37, 80, 14, "Tempo sync", TR_NOOP ("Tempo sync")));
    v.push_back (toggle (142, 55, 80, 14, "Keep pitch", TR_NOOP ("Keep pitch")));
    v.push_back (slider (Item::hslider, 145, 80, 60, 30, "Time smoothing", TR_NOOP ("Smoothing")));
    v.push_back (knob (233, 82, 34, "Time R / pan", TR_NOOP ("Offset"), 104, bipolar));

    v.push_back (section (257, 2, 121, 110, TR_NOOP ("Delay model")));
    v.push_back (radio (268, 36, 62, 52, "Delay type", TR_NOOP ("Mono|Stereo|Ping pong|Off"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (knob (356, 80, 34, "Stereo spread", TR_NOOP ("Stereo"), 104));

    v.push_back (section (380, 2, 305, 110, TR_NOOP ("Feedback")));
    v.push_back (knob (418, 70, 56, "Feedback", TR_NOOP ("Level"), 104, big));
    v.push_back (knob (469, 80, 34, "Cutoff", TR_NOOP ("Cutoff"), 104));
    v.push_back (knob (516, 80, 34, "Resonance", TR_NOOP ("Res"), 104));
    v.push_back (radio (557, 36, 40, 52, "Filter mode", TR_NOOP ("LP|HP|BP|Off"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (knob (620, 80, 34, "Rate reduce", TR_NOOP ("Smp rate"), 104));
    v.push_back (knob (666, 80, 34, "Bit reduce", TR_NOOP ("Bits"), 104));

    v.push_back (section (688, 2, 62, 216, TR_NOOP ("Output")));
    v.push_back (knob (719, 48, 34, "Wet", TR_NOOP ("Wet"), 80));
    v.push_back (knob (719, 110, 34, "Colour", TR_NOOP ("Tone"), 142, bipolar));
    v.push_back (knob (719, 172, 34, "Dry", TR_NOOP ("Dry"), 204));

    v.push_back (section (62, 114, 193, 104, TR_NOOP ("Modulation")));
    v.push_back (knob (94, 172, 44, "Mod rate", TR_NOOP ("Rate"), 206, big));
    v.push_back (knob (180, 172, 34, "Mod amount", TR_NOOP ("Time"), 206));
    v.push_back (knob (231, 172, 34, "Cutoff mod", TR_NOOP ("Cutoff"), 206));

    v.push_back (section (257, 114, 121, 104, TR_NOOP ("Diffusion")));
    v.push_back (knob (292, 172, 34, "Diffusion", TR_NOOP ("Level"), 206));
    v.push_back (knob (343, 172, 34, "Diffusion spread", TR_NOOP ("Spread"), 206));

    v.push_back (section (380, 114, 305, 104, TR_NOOP ("Feedback distortion")));
    v.push_back (radio (392, 160, 44, 28, "Saturation type", TR_NOOP ("Limit|Sat")));
    v.push_back (knob (476, 172, 34, "Distortion", TR_NOOP ("Level"), 206));
    v.push_back (knob (524, 172, 34, "Hardness", TR_NOOP ("Knee"), 206));
    v.push_back (knob (572, 172, 34, "Symmetry", TR_NOOP ("Symmetry"), 206, bipolar));
    v.push_back (display (614, 140, 68, 72, "shaper", "Distortion"));
    return l;
}

Layout fruityMultiband()
{
    // input meter strip, IIR/FIR, limiter, output gain | band graph with the four crossover knobs | LOW, MID and
    // HIGH bands (A/M/B mode, threshold, ratio, knee, attack, release, gain), each followed by its meters
    Layout l;
    l.fl = "Fruity Multiband Compressor";
    l.width = 805;
    l.height = 160;
    auto& v = l.items;
    v.push_back (display (8, 7, 14, 128, "meter"));
    v.push_back (radio (34, 7, 40, 26, "Linear phase", "IIR|FIR"));
    v.push_back (label (26, 44, 48, 13, TR_NOOP ("Limiter")));
    v.push_back (toggle (40, 58, 20, 14, "Limiter", nullptr));
    v.push_back (knob (50, 118, 34, "Output gain", TR_NOOP ("Gain"), 144, bipolar));
    v.push_back (section (79, 7, 200, 142, nullptr, boxed));
    v.push_back (display (83, 11, 192, 78, "multiband"));
    v.push_back (label (86, 90, 186, 12, TR_NOOP ("Crossovers"), dim));
    v.push_back (knob (103, 121, 34, "Low crossover", nullptr, 0, noCaption));
    v.push_back (knob (153, 121, 34, "Mid lower crossover", nullptr, 0, noCaption));
    v.push_back (knob (203, 121, 34, "Mid upper crossover", nullptr, 0, noCaption));
    v.push_back (knob (254, 121, 34, "High crossover", nullptr, 0, noCaption));

    constexpr int bandColours[] = { 0, 2, 5 }; // low / mid / high: the editor's red, yellow and blue band colours
    v[8].colour = bandColours[0];  // crossover knobs: the band each one bounds from below
    v[9].colour = bandColours[1];
    v[10].colour = bandColours[1];
    v[11].colour = bandColours[2];
    struct Band { const char* title; const char *mode, *threshold, *ratio, *knee, *attack, *release, *gain; };
    const Band bands[] = { { TR_NOOP ("Low band"), "Low mode", "Low threshold", "Low ratio", "Low knee", "Low attack", "Low release", "Low gain" },
                           { TR_NOOP ("Mid band"), "Mid mode", "Mid threshold", "Mid ratio", "Mid knee", "Mid attack", "Mid release", "Mid gain" },
                           { TR_NOOP ("High band"), "High mode", "High threshold", "High ratio", "High knee", "High attack", "High release", "High gain" } };
    for (int b = 0; b < 3; ++b)
    {
        const auto x = 287.0f + 172.0f * (float) b;
        const auto& band = bands[b];
        v.push_back (section (x, 7, 130, 16, nullptr, boxed));
        v.push_back (tabs (x + 2, 8, 48, 14, band.mode, "A|M|B", { 0.0f, 0.5f, 1.0f }));
        v.push_back (label (x + 52, 8, 76, 14, band.title));
        const auto first = v.size();
        v.push_back (knob (x + 19, 58, 32, band.threshold, TR_NOOP ("Thres"), 84));
        v.push_back (knob (x + 65, 58, 32, band.ratio, TR_NOOP ("Ratio"), 84));
        v.push_back (knob (x + 111, 58, 32, band.knee, TR_NOOP ("Knee"), 84));
        v.push_back (knob (x + 19, 120, 32, band.attack, TR_NOOP ("Att"), 144));
        v.push_back (knob (x + 65, 120, 32, band.release, TR_NOOP ("Rel"), 144));
        v.push_back (knob (x + 111, 120, 32, band.gain, TR_NOOP ("Gain"), 144, bipolar));
        for (auto k = first; k < v.size(); ++k)
            v[k].colour = bandColours[b];
        v.push_back (display (x + 140, 7, 24, 128, "meter"));
    }
    return l;
}

Layout reeverb2()
{
    Layout l;
    l.fl = "Fruity Reeverb 2";
    l.width = 538;
    l.height = 166;
    auto& v = l.items;
    v.push_back (display (2, 2, 160, 160, "room", "Room size"));
    v.push_back (section (164, 2, 58, 160, nullptr, boxed));
    v.push_back (radio (176, 20, 44, 28, "Mid/side input", TR_NOOP ("Mid|Side"), { 0.0f, 1.0f }));
    v.push_back (knob (193, 75, 30, "High cut", TR_NOOP ("H.cut"), 100));
    v.push_back (knob (193, 126, 30, "Low cut", TR_NOOP ("L.cut"), 151));
    v.push_back (section (224, 2, 102, 160, nullptr, boxed));
    v.push_back (knob (251, 23, 30, "Predelay", TR_NOOP ("Delay"), 48));
    v.push_back (toggle (290, 26, 20, 14, "Tempo predelay", nullptr));
    v.push_back (label (281, 42, 36, 12, TR_NOOP ("Tempo")));
    v.push_back (knob (251, 74, 30, "Room size", TR_NOOP ("Size"), 100));
    v.push_back (knob (297, 74, 30, "Mod depth", TR_NOOP ("Mod"), 100));
    v.push_back (knob (251, 125, 30, "Diffusion", TR_NOOP ("Diff"), 151));
    v.push_back (knob (297, 125, 30, "Mod speed", TR_NOOP ("Speed"), 151));
    v.push_back (section (328, 2, 100, 160, nullptr, boxed));
    v.push_back (knob (355, 23, 30, "Bass mult.", TR_NOOP ("Bass"), 48));
    v.push_back (knob (398, 23, 30, "Decay", TR_NOOP ("Dec"), 48));
    v.push_back (knob (355, 74, 30, "Crossover", TR_NOOP ("Cross"), 100));
    v.push_back (knob (398, 74, 30, "High damping", TR_NOOP ("Damp"), 100));
    v.push_back (label (334, 118, 90, 36, "Reeverb 2", dim));
    v.push_back (section (430, 2, 106, 160, nullptr, boxed));
    v.push_back (slider (Item::vslider, 440, 6, 20, 98, "Dry", TR_NOOP ("Dry")));
    v.push_back (slider (Item::vslider, 471, 6, 20, 98, "Early ref.", TR_NOOP ("ER")));
    v.push_back (slider (Item::vslider, 503, 6, 20, 98, "Wet", TR_NOOP ("Wet")));
    v.push_back (knob (481, 124, 30, "Stereo sep.", TR_NOOP ("Sep"), 151, bipolar));
    return l;
}

Layout luxeVerb()
{
    // INPUT | REVERB | PITCH | OUTPUT on top, ENVELOPE and the analyser below; every knob has its value on a plate above
    Layout l;
    l.fl = "LuxeVerb";
    l.width = 947;
    l.height = 456;
    auto& v = l.items;
    const auto tile = [&v] (float cx, float cy, const char* param, const char* caption, float captionY, std::uint16_t style = 0) {
        v.push_back (knob (cx, cy, 34, param, caption, captionY, style | valueAbove));
    };
    const auto fader = [&v] (float cx, const char* param, const char* caption) {
        v.push_back (value (cx - 19, 46, 38, 13, param, 0));
        v.push_back (slider (Item::vslider, cx - 12, 62, 24, 145, param, caption));
    };

    v.push_back (section (8, 14, 116, 200, TR_NOOP ("Input")));
    fader (37, "Input wet gain", TR_NOOP ("Wet gain"));
    tile (93, 82, "Input high cut", TR_NOOP ("High cut"), 108);
    tile (93, 174, "Input low cut", TR_NOOP ("Low cut"), 200);

    v.push_back (section (130, 14, 326, 200, TR_NOOP ("Reverb")));
    v.push_back (toggle (430, 16, 22, 12, "Reverb", nullptr));
    v.push_back (value (140, 46, 70, 13, "Decay", 0));
    v.push_back (knob (174, 125, 62, "Decay", TR_NOOP ("Decay"), 200, big));
    tile (244, 82, "Brightness", TR_NOOP ("Brightness"), 108);
    tile (303, 82, "Size", TR_NOOP ("Size"), 108);
    tile (363, 82, "Diffusion", TR_NOOP ("Diffusion"), 108);
    tile (422, 82, "Character", TR_NOOP ("Character"), 108);
    v.push_back (when (knob (244, 174, 34, "Pre-delay", TR_NOOP ("P. delay"), 200, valueAbove), "Pre-delay sync", 0.0f, 0.5f));
    v.push_back (when (knob (244, 174, 34, "Pre-delay (sync)", TR_NOOP ("P. delay"), 200, valueAbove), "Pre-delay sync", 0.5f, 1.0f));
    v.push_back (toggle (262, 194, 11, 11, "Pre-delay sync", "T", boxed));
    tile (303, 174, "Mod amount", TR_NOOP ("Mod amp"), 200);
    tile (363, 174, "Mod frequency", TR_NOOP ("Mod freq"), 200);
    v.push_back (radio (397, 130, 56, 54, "Freeze mode", TR_NOOP ("Normal|Freeze|Sustain")));
    v.push_back (toggle (407, 194, 44, 12, "High quality", TR_NOOP ("HQ")));

    v.push_back (section (462, 14, 238, 200, TR_NOOP ("Pitch")));
    v.push_back (tabs (530, 16, 120, 14, "Pitch type", TR_NOOP ("Feedback|Feedforward")));
    v.push_back (toggle (672, 16, 22, 12, "Feedback", nullptr));
    // feed-forward shifters
    {
        const auto ff = [] (LayoutItem i) { return when (std::move (i), "Pitch type", 0.5f, 1.0f); };
        v.push_back (ff (value (476, 46, 38, 13, "Feed-forward unpitched", 0)));
        v.push_back (ff (slider (Item::vslider, 483, 62, 24, 145, "Feed-forward unpitched", TR_NOOP ("Direct amp"))));
        v.push_back (ff (knob (552, 82, 34, "Feed-forward pitch 1", TR_NOOP ("Pitch 1"), 108, valueAbove | bipolar)));
        v.push_back (ff (knob (607, 82, 34, "Feed-forward amount 1", TR_NOOP ("Amp 1"), 108, valueAbove)));
        v.push_back (ff (when (knob (664, 82, 34, "Feed-forward delay 1", TR_NOOP ("Delay 1"), 108, valueAbove), "Feed-forward delay 1 sync", 0.0f, 0.5f)));
        v.push_back (ff (knob (552, 174, 34, "Feed-forward pitch 2", TR_NOOP ("Pitch 2"), 200, valueAbove | bipolar)));
        v.push_back (ff (knob (607, 174, 34, "Feed-forward amount 2", TR_NOOP ("Amp 2"), 200, valueAbove)));
        v.push_back (ff (when (knob (664, 174, 34, "Feed-forward delay 2", TR_NOOP ("Delay 2"), 200, valueAbove), "Feed-forward delay 2 sync", 0.0f, 0.5f)));
        // (the synced delays replace the free ones while T is on)
        v.push_back (ff (when (knob (664, 82, 34, "Feed-forward delay 1 (sync)", TR_NOOP ("Delay 1"), 108, valueAbove), "Feed-forward delay 1 sync", 0.5f, 1.0f)));
        v.push_back (ff (when (knob (664, 174, 34, "Feed-forward delay 2 (sync)", TR_NOOP ("Delay 2"), 200, valueAbove), "Feed-forward delay 2 sync", 0.5f, 1.0f)));
        v.push_back (ff (toggle (683, 102, 11, 11, "Feed-forward delay 1 sync", "T", boxed)));
        v.push_back (ff (toggle (683, 194, 11, 11, "Feed-forward delay 2 sync", "T", boxed)));
    }
    // feedback shifter: cuts, pitch shift and gain faders, delay, reverb mix
    {
        const auto fb = [] (LayoutItem i) { return when (std::move (i), "Pitch type", 0.0f, 0.5f); };
        v.push_back (fb (knob (496, 82, 34, "Feedback high cut", TR_NOOP ("High cut"), 108, valueAbove)));
        v.push_back (fb (knob (496, 174, 34, "Feedback low cut", TR_NOOP ("Low cut"), 200, valueAbove)));
        v.push_back (fb (value (534, 46, 38, 13, "Feedback pitch", 0)));
        v.push_back (fb (slider (Item::vslider, 541, 62, 24, 145, "Feedback pitch", TR_NOOP ("Pitch shift"), bipolar)));
        v.push_back (fb (value (589, 46, 38, 13, "Feedback gain", 0)));
        v.push_back (fb (slider (Item::vslider, 596, 62, 24, 145, "Feedback gain", TR_NOOP ("Gain"))));
        v.push_back (fb (when (knob (665, 82, 34, "Feedback delay", TR_NOOP ("Delay"), 108, valueAbove), "Feedback delay sync", 0.0f, 0.5f)));
        v.push_back (fb (when (knob (665, 82, 34, "Feedback delay (sync)", TR_NOOP ("Delay"), 108, valueAbove), "Feedback delay sync", 0.5f, 1.0f)));
        v.push_back (fb (toggle (683, 102, 11, 11, "Feedback delay sync", "T", boxed)));
        v.push_back (fb (knob (665, 174, 34, "Feedback reverb mix", TR_NOOP ("Reverb mix"), 200, valueAbove)));
    }

    v.push_back (section (706, 14, 234, 200, TR_NOOP ("Output")));
    fader (736, "Dry level", TR_NOOP ("Dry"));
    fader (791, "Wet level", TR_NOOP ("Wet"));
    v.push_back (toggle (802, 194, 11, 11, "Limiter", "L", boxed));
    tile (847, 82, "Peak frequency", TR_NOOP ("Peak freq"), 108);
    tile (907, 82, "Peak gain", TR_NOOP ("Peak gain"), 108, bipolar);
    tile (847, 174, "Peak Q", TR_NOOP ("Peak Q"), 200);
    tile (907, 174, "Stereo width", TR_NOOP ("Width"), 200);

    v.push_back (section (8, 236, 932, 216, TR_NOOP ("Envelope")));
    v.push_back (radio (16, 278, 48, 54, "Envelope mode", TR_NOOP ("Off|Wet|Decay")));
    tile (93, 309, "Envelope threshold", TR_NOOP ("Threshold"), 335);
    tile (150, 309, "Envelope scale", TR_NOOP ("Scale"), 335, bipolar);
    tile (207, 309, "Envelope low cut", TR_NOOP ("Low cut"), 335);
    tile (264, 309, "Envelope offset", TR_NOOP ("Offset"), 335, bipolar);
    v.push_back (stepper (20, 380, 32, 34, "Envelope sidechain", TR_NOOP ("Sidechain"), 0));
    tile (93, 399, "Envelope attack", TR_NOOP ("Attack"), 425);
    tile (150, 399, "Envelope decay", TR_NOOP ("Decay"), 425);
    tile (207, 399, "Envelope smoothing", TR_NOOP ("Smooth"), 425);
    v.push_back (display (298, 262, 636, 184, "envelope", "Decay"));
    return l;
}

Layout parametricEq2()
{
    // the response curve (octave and band-name rows above, frequencies below, dB scale at its right), the output
    // fader, then one column per band: type button, gain fader, FREQ and BW knobs
    Layout l;
    l.fl = "Fruity Parametric EQ 2";
    l.width = 707;
    l.height = 356;
    auto& v = l.items;
    v.push_back (display (8, 4, 524, 292, "eq"));
    v.push_back (slider (Item::vslider, 532, 22, 16, 270, "Output", nullptr, noCaption | bipolar));
    v.push_back (label (510, 310, 38, 12, TR_NOOP ("Freq"), dim));
    v.push_back (label (510, 334, 38, 12, TR_NOOP ("BW"), dim));
    static const char* const names[peq::bands][5] = {
        { "B1 type", "B1 freq.", "B1 gain", "B1 Q", "B1 slope" }, { "B2 type", "B2 freq.", "B2 gain", "B2 Q", "B2 slope" },
        { "B3 type", "B3 freq.", "B3 gain", "B3 Q", "B3 slope" }, { "B4 type", "B4 freq.", "B4 gain", "B4 Q", "B4 slope" },
        { "B5 type", "B5 freq.", "B5 gain", "B5 Q", "B5 slope" }, { "B6 type", "B6 freq.", "B6 gain", "B6 Q", "B6 slope" },
        { "B7 type", "B7 freq.", "B7 gain", "B7 Q", "B7 slope" } };
    std::vector<float> typeValues;
    for (int t = 0; t < peq::bandTypes; ++t)
        typeValues.push_back (peq::typeValue ((peq::BandType) t));
    for (int b = 0; b < peq::bands; ++b)
    {
        const auto x = 552.0f + 21.0f * (float) b;
        LayoutItem type;
        type.type = Item::menu;
        type.x = x, type.y = 4, type.w = 16, type.h = 24;
        type.param = names[b][0];
        type.options = TR_NOOP ("Off|Peak|Low shelf|High shelf|Low cut|High cut|Band-pass|Notch");
        type.values = typeValues;
        type.colour = b;
        v.push_back (type);
        auto gain = slider (Item::vslider, x, 30, 16, 262, names[b][2], nullptr, noCaption | bipolar);
        gain.colour = b;
        v.push_back (gain);
        auto freq = knob (x + 8, 316, 19, names[b][1], nullptr, 0, noCaption);
        freq.colour = b;
        v.push_back (freq);
        auto width = knob (x + 8, 340, 19, names[b][3], nullptr, 0, noCaption);
        width.colour = b;
        v.push_back (width);
    }
    v.push_back (display (8, 300, 492, 52, "eqinfo"));
    for (int b = 0; b < peq::bands; ++b)
        l.editedElsewhere.push_back (names[b][4]); // the band menu (right-click a handle or the type button)
    return l;
}

Layout grossBeat()
{
    // left: the time panel (mix knob), HOLD, the time slots, the volume slots, ATT / REL / TNS, the volume panel; right:
    // the envelope of the selected slots and the slot's settings under it
    Layout l;
    l.fl = "Gross Beat";
    l.width = 840;
    l.height = 563;
    auto& v = l.items;
    static const std::string slots = [] {
        std::string out;
        for (int i = 1; i <= 36; ++i)
            out += (i > 1 ? "|" : "") + std::string ("Slot ") + std::to_string (i);
        return out;
    }();
    v.push_back (section (0, 0, 268, 563, nullptr, boxed));
    v.push_back (section (6, 7, 128, 46, nullptr, boxed | raised));
    v.push_back (knob (31, 30, 40, "Time mix", nullptr, 0, noCaption));
    v.push_back (label (56, 20, 70, 20, TR_NOOP ("Time"), alignLeft));
    v.push_back (display (189, 12, 72, 72, "gbjog"));
    v.push_back (toggle (113, 62, 62, 28, "Hold", TR_NOOP ("Hold"), boxed));
    auto timeSlots = radio (13, 103, 246, 162, "Time slot", slots.c_str());
    timeSlots.columns = 4;
    v.push_back (timeSlots);
    auto volumeSlots = radio (13, 276, 246, 162, "Volume slot", slots.c_str());
    volumeSlots.columns = 4;
    volumeSlots.colour = 1;
    v.push_back (volumeSlots);
    v.push_back (knob (80, 467, 38, "Attack", TR_NOOP ("Att"), 492));
    v.push_back (knob (130, 467, 38, "Release", TR_NOOP ("Rel"), 492));
    v.push_back (knob (186, 467, 38, "Tension", TR_NOOP ("Tns"), 492, bipolar));
    v.push_back (section (6, 509, 128, 46, nullptr, boxed | raised));
    v.push_back (knob (31, 531, 40, "Volume mix", nullptr, 0, noCaption));
    v.push_back (label (56, 521, 70, 20, TR_NOOP ("Volume"), alignLeft));
    v.push_back (display (272, 4, 556, 530, "grossbeat"));
    v.push_back (display (272, 537, 556, 22, "gbinfo"));
    return l;
}

Layout reeverb1()
{
    auto l = fruityGrid ("Fruity Reeverb", 5, 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Low cut", TR_NOOP ("Lowcut"));
    fruityCell (v, 1, 0, "High cut", TR_NOOP ("Highcut"));
    fruityCell (v, 0, 1, "Predelay", TR_NOOP ("Predelay"));
    fruityCell (v, 1, 1, "Room size", TR_NOOP ("Room size"));
    fruityCell (v, 0, 2, "Diffusion", TR_NOOP ("Diffusion"));
    fruityCell (v, 1, 2, "Color", TR_NOOP ("Color"));
    fruityCell (v, 0, 3, "Decay", TR_NOOP ("Decay"));
    fruityCell (v, 1, 3, "High damping", TR_NOOP ("High damping"));
    fruityCell (v, 0, 4, "Dry", TR_NOOP ("Dry"));
    fruityCell (v, 1, 4, "Wet", TR_NOOP ("Reverb"));
    return l;
}

Layout fruityFlangus()
{
    // the order bar, then seven tall faders in their columns
    Layout l;
    l.fl = "Fruity Flangus";
    l.width = 366;
    l.height = 176;
    auto& v = l.items;
    auto order = slider (Item::vslider, 8, 75, 31, 76, "Order", nullptr, noCaption);
    order.steps = 8;
    v.push_back (order);
    v.push_back (label (2, 158, 44, 16, TR_NOOP ("Ord")));
    static const char* const params[] = { "Depth", "Speed", "Delay", "Spread", "Cross", "Dry", "Wet" };
    static const char* const captions[] = { TR_NOOP ("Depth"), TR_NOOP ("Spd"), TR_NOOP ("Del"), TR_NOOP ("Sprd"), TR_NOOP ("Cross"),
                                            TR_NOOP ("Dry"), TR_NOOP ("Wet") };
    for (int i = 0; i < 7; ++i)
    {
        const auto x = 43.0f + 46.0f * (float) i;
        v.push_back (section (x, 9, 40, 142, nullptr, boxed));
        v.push_back (slider (Item::vslider, x + 6, 14, 28, 132, params[i], nullptr, noCaption));
        v.push_back (label (x - 4, 158, 48, 16, captions[i]));
    }
    return l;
}

Layout fruitySoftClipper()
{
    // THRES and POST, the plugin's name plate, the output meter strip and the transfer curve
    Layout l;
    l.fl = "Fruity Soft Clipper";
    l.width = 274;
    l.height = 133;
    auto& v = l.items;
    v.push_back (section (0, 0, 116, 133, nullptr, boxed));
    v.push_back (knob (37, 55, 36, "Threshold", TR_NOOP ("Thres"), 88));
    v.push_back (knob (89, 55, 36, "Post", TR_NOOP ("Post"), 88));
    v.push_back (label (6, 102, 104, 24, TR_NOOP ("Soft clipper"), boxed));
    v.push_back (display (118, 3, 24, 127, "meter"));
    v.push_back (display (144, 3, 130, 127, "softclip", "Threshold"));
    return l;
}

Layout fruityBloodOverdrive()
{
    // PRE BAND and COLOR | the big PRE AMP with its x100 switch | POST FILTER and POST GAIN; captions on plates
    Layout l;
    l.fl = "Fruity Blood Overdrive";
    l.width = 428;
    l.height = 186;
    auto& v = l.items;
    v.push_back (section (0, 0, 133, 186, nullptr, boxed));
    v.push_back (section (135, 0, 158, 186, nullptr, boxed | raised));
    v.push_back (section (295, 0, 133, 186, nullptr, boxed));
    v.push_back (knob (69, 37, 52, "Pre band", nullptr, 0, noCaption));
    v.push_back (label (30, 69, 78, 15, TR_NOOP ("Pre band"), boxed));
    v.push_back (knob (69, 124, 52, "Color", nullptr, 0, noCaption | bipolar));
    v.push_back (label (30, 157, 78, 15, TR_NOOP ("Color"), boxed));
    v.push_back (knob (209, 86, 96, "Pre amp", nullptr, 0, noCaption | big));
    v.push_back (label (174, 157, 66, 15, TR_NOOP ("Pre amp"), boxed));
    v.push_back (toggle (244, 157, 44, 15, "x100", "x100"));
    v.push_back (knob (362, 36, 52, "Post filter", nullptr, 0, noCaption));
    v.push_back (label (323, 68, 78, 15, TR_NOOP ("Post filter"), boxed));
    v.push_back (knob (362, 124, 52, "Post gain", nullptr, 0, noCaption));
    v.push_back (label (323, 157, 78, 15, TR_NOOP ("Post gain"), boxed));
    return l;
}

Layout fruityPanOMatic()
{
    // PAN and VOL; the LFO (shape buttons, amount, speed, off / pan / volume target); the pan / volume pad
    Layout l;
    l.fl = "Fruity PanOMatic";
    l.width = 307;
    l.height = 166;
    auto& v = l.items;
    v.push_back (section (0, 0, 144, 166, nullptr, boxed));
    v.push_back (knob (34, 32, 30, "Pan", TR_NOOP ("Pan"), 53, bipolar));
    v.push_back (knob (77, 32, 30, "Volume", TR_NOOP ("Vol"), 53));
    v.push_back (section (0, 66, 144, 100, nullptr, boxed | raised));
    v.push_back (label (4, 70, 40, 20, TR_NOOP ("LFO"), alignLeft));
    auto shapes = radio (60, 80, 78, 24, "LFO shape", TR_NOOP ("Sine|Triangle|Square"));
    shapes.columns = 3;
    v.push_back (shapes);
    v.push_back (knob (34, 124, 30, "LFO amount", TR_NOOP ("Amt"), 150, bipolar));
    v.push_back (knob (77, 124, 30, "LFO speed", TR_NOOP ("Spd"), 150));
    v.push_back (radio (101, 112, 40, 40, "LFO target", TR_NOOP ("Off|Pan|Vol")));
    v.push_back (display (146, 3, 158, 160, "panpad"));
    return l;
}

Layout stereoShaper()
{
    // MIXER MATRIX: the four faders (R→L, left, right, L→R) with the LEFT / RIGHT sums under them; EFFECTS: pre / post,
    // DELAY and PHASE; the in / out difference switch
    Layout l;
    l.fl = "Fruity Stereo Shaper";
    l.width = 276;
    l.height = 316;
    auto& v = l.items;
    v.push_back (label (6, 0, 110, 24, TR_NOOP ("Mixer matrix"), alignLeft));
    v.push_back (label (190, 0, 80, 24, TR_NOOP ("Effects")));
    v.push_back (section (20, 30, 160, 270, nullptr, boxed));
    static const char* const faders[] = { "R > L", "Left", "Right", "L > R" };
    for (int i = 0; i < 4; ++i)
        v.push_back (slider (Item::vslider, 31.0f + 39.0f * (float) i, 64, 20, 216, faders[i], nullptr, noCaption | bipolar));
    v.push_back (label (30, 286, 60, 14, TR_NOOP ("Left")));
    v.push_back (label (108, 286, 60, 14, TR_NOOP ("Right")));
    v.push_back (radio (190, 44, 84, 26, "Effects post", TR_NOOP ("Pre|Post")));
    v.push_back (knob (233, 129, 50, "Delay", TR_NOOP ("Delay"), 164, bipolar));
    v.push_back (knob (233, 221, 50, "Phase", TR_NOOP ("Phase"), 257, bipolar));
    v.push_back (label (178, 280, 60, 26, TR_NOOP ("In/out difference"), alignLeft));
    v.push_back (toggle (238, 284, 34, 18, "Difference", TR_NOOP ("On"), boxed));
    return l;
}

Layout transientProcessor()
{
    // IN meter | SPLIT FREQ; ATTACK panel (knob, shape); DRIVE, SPLIT BALANCE; RELEASE panel (knob, shape); GAIN | OUT
    // meter; on / off; the gain view under it
    Layout l;
    l.fl = "Transient Processor";
    l.width = 553;
    l.height = 325;
    auto& v = l.items;
    v.push_back (display (2, 3, 30, 172, "meter"));
    v.push_back (display (518, 3, 32, 172, "meter"));
    v.push_back (section (34, 3, 482, 172, nullptr, boxed));
    v.push_back (label (150, 6, 250, 20, "Transient Processor", dim));
    v.push_back (knob (80, 52, 40, "Split freq.", TR_NOOP ("Split freq."), 86));
    v.push_back (section (126, 27, 110, 148, nullptr, boxed | raised));
    v.push_back (knob (181, 72, 66, "Attack", TR_NOOP ("Attack"), 129, big | bipolar));
    v.push_back (slider (Item::hslider, 156, 143, 50, 22, "Attack shape", nullptr, noCaption | bipolar));
    v.push_back (knob (279, 52, 40, "Drive", TR_NOOP ("Drive"), 86));
    v.push_back (knob (279, 140, 40, "Split balance", TR_NOOP ("Split balance"), 168, bipolar));
    v.push_back (section (322, 27, 110, 148, nullptr, boxed | raised));
    v.push_back (knob (377, 72, 66, "Release", TR_NOOP ("Release"), 129, big | bipolar));
    v.push_back (slider (Item::hslider, 352, 143, 50, 22, "Release shape", nullptr, noCaption | bipolar));
    v.push_back (knob (469, 52, 40, "Gain", TR_NOOP ("Gain"), 86, bipolar));
    v.push_back (radio (40, 146, 44, 28, "On", TR_NOOP ("Off|On")));
    v.push_back (display (8, 180, 542, 142, "envelope", "Release"));
    return l;
}

Layout fruityFreeFilter()
{
    auto l = fruityGrid ("Fruity Free Filter", 2, 3);
    l.width = 418;
    auto& v = l.items;
    fruityCell (v, 0, 0, "Type", TR_NOOP ("Type"));
    fruityCell (v, 1, 0, "Frequency", TR_NOOP ("Freq"));
    fruityHalfCell (v, 2, 0, 0, "Center", TR_NOOP ("Center"));
    fruityCell (v, 0, 1, "Q", TR_NOOP ("Q"), Cell::slider);
    fruityCell (v, 1, 1, "Gain", TR_NOOP ("Gain"));
    v.push_back (display (340, 60, 70, 40, "filter"));
    return l;
}

Layout fruityWaveShaper()
{
    // the shaping curve; the plugin's name, unipolar / bipolar, PRE, MIX, POST; smooth, oversample, HQ, center
    Layout l;
    l.fl = "Fruity WaveShaper";
    l.width = 277;
    l.height = 358;
    auto& v = l.items;
    v.push_back (display (8, 3, 258, 234, "waveshaper"));
    v.push_back (section (0, 254, 277, 104, nullptr, boxed));
    v.push_back (label (4, 262, 62, 40, "WaveShaper", dim));
    v.push_back (radio (72, 266, 46, 34, "Unipolar", TR_NOOP ("Bipolar|Unipolar")));
    v.push_back (knob (165, 279, 30, "Pre amp", TR_NOOP ("Pre"), 302));
    v.push_back (knob (210, 279, 30, "Mix", TR_NOOP ("Mix"), 302));
    v.push_back (knob (253, 279, 30, "Post gain", TR_NOOP ("Post"), 302));
    v.push_back (toggle (150, 312, 34, 14, "Smooth", TR_NOOP ("Smooth")));
    v.push_back (toggle (150, 332, 34, 14, "HQ", TR_NOOP ("HQ")));
    v.push_back (toggle (190, 332, 60, 14, "Center", TR_NOOP ("Center")));
    v.push_back (label (188, 312, 60, 14, TR_NOOP ("Oversample"), alignLeft));
    v.push_back (stepper (250, 310, 24, 18, "Oversampling", nullptr, 4));
    return l;
}

Layout vintageChorus()
{
    // MODE (I / II / edit) | DELAYS (time 1, time 2, feedback, high-pass faders) | MODULATION (start phase, wave,
    // L/R phase, tempo sync, speed fader) | LEVELS (mono input, invert wet, noise gate, noise, gain, mix fader)
    Layout l;
    l.fl = "Vintage Chorus";
    l.width = 754;
    l.height = 214;
    auto& v = l.items;
    v.push_back (label (420, 2, 326, 36, "Vintage Chorus", dim));
    v.push_back (section (14, 47, 173, 160, TR_NOOP ("Mode")));
    v.push_back (tabs (28, 90, 146, 64, "Mode", TR_NOOP ("I|II|Edit")));
    v.push_back (section (190, 47, 174, 160, TR_NOOP ("Delays")));
    static const char* const delays[] = { "Delay time 1", "Delay time 2", "Feedback", "Wet HP freq" };
    static const char* const delayCaptions[] = { "1", "2", TR_NOOP ("Feedback"), TR_NOOP ("H.pass") };
    for (int i = 0; i < 4; ++i)
    {
        const auto x = i < 2 ? 210.0f + 33.0f * (float) i : 282.0f + 39.0f * (float) (i - 2);
        v.push_back (slider (Item::vslider, x, 70, 20, 112, delays[i], nullptr, noCaption));
        v.push_back (label (x - 14, 183, 48, 12, delayCaptions[i]));
    }
    v.push_back (label (214, 195, 48, 10, TR_NOOP ("Time"), dim));
    v.push_back (section (367, 47, 195, 160, TR_NOOP ("Modulation")));
    v.push_back (knob (401, 96, 30, "LFO start phase", TR_NOOP ("Start phase"), 127));
    v.push_back (radio (444, 82, 40, 34, "LFO wave", TR_NOOP ("Triangle|Sine")));
    v.push_back (knob (401, 158, 30, "LFO L/R phase", TR_NOOP ("L/R phase"), 189));
    v.push_back (toggle (436, 160, 70, 14, "LFO sync", TR_NOOP ("Tempo sync")));
    v.push_back (slider (Item::vslider, 516, 70, 20, 124, "LFO speed", TR_NOOP ("Speed")));
    v.push_back (section (565, 47, 177, 160, TR_NOOP ("Levels")));
    v.push_back (toggle (574, 118, 76, 14, "Mono input", TR_NOOP ("Mono input")));
    v.push_back (toggle (574, 140, 76, 14, "Wet polarity", TR_NOOP ("Invert wet")));
    v.push_back (toggle (574, 162, 76, 14, "Noise gate", TR_NOOP ("Noise gate")));
    v.push_back (knob (663, 96, 30, "Hiss", TR_NOOP ("Noise"), 127));
    v.push_back (knob (663, 158, 30, "Gain", TR_NOOP ("Gain"), 189));
    v.push_back (slider (Item::vslider, 698, 70, 20, 124, "Wet/dry mix", TR_NOOP ("Mix")));
    l.menuSwitches = { "Link Chorus 1&2" };
    return l;
}

Layout hyperChorus()
{
    // three movement pads and the voices' view on top, one row of knobs under them
    Layout l;
    l.fl = "Hyper Chorus";
    l.width = 754;
    l.height = 214;
    auto& v = l.items;
    v.push_back (label (500, 2, 246, 32, "Hyper Chorus", dim));
    v.push_back (display (27, 38, 100, 102, "xy", "Delay", "Feedback"));
    v.push_back (display (157, 38, 100, 102, "xy", "Mod amount", "Mod rate"));
    v.push_back (display (288, 38, 100, 102, "xy", "Movement", "Movement speed"));
    v.push_back (display (420, 38, 300, 102, "voices"));
    v.push_back (section (2, 146, 750, 66, nullptr, boxed | raised));
    struct K { float x; const char* param; const char* caption; };
    const K knobs[] = { { 44, "Delay", TR_NOOP ("Delay") }, { 108, "Feedback", TR_NOOP ("Feedback") }, { 175, "Mod amount", TR_NOOP ("Modulation") },
                        { 239, "Mod rate", TR_NOOP ("Mod rate") }, { 304, "Movement", TR_NOOP ("Movement") },
                        { 369, "Movement speed", TR_NOOP ("Move rate") }, { 514, "High pass", TR_NOOP ("High pass") },
                        { 569, "Low pass", TR_NOOP ("Low pass") }, { 644, "LFO phase difference", TR_NOOP ("LFO phase dif") },
                        { 710, "Dry/wet", TR_NOOP ("Mix") } };
    for (const auto& k : knobs)
        v.push_back (knob (k.x, 171, 32, k.param, k.caption, 202));
    v.push_back (toggle (436, 162, 30, 18, "ArachnaChor mode", nullptr));
    v.push_back (label (420, 196, 60, 12, TR_NOOP ("Delay lines")));
    return l;
}

Layout fruityBalance()
{
    // Balance and Volume knobs (captions above, values under them) around the level meter
    Layout l;
    l.fl = "Fruity Balance";
    l.width = 232;
    l.height = 150;
    auto& v = l.items;
    v.push_back (section (0, 0, 232, 150, nullptr, boxed));
    v.push_back (label (8, 18, 80, 20, TR_NOOP ("Balance")));
    v.push_back (knob (48, 77, 46, "Balance", nullptr, 0, noCaption | bipolar));
    v.push_back (value (8, 112, 80, 20, "Balance", 0));
    v.push_back (display (90, 16, 40, 118, "meter"));
    v.push_back (label (130, 18, 80, 20, TR_NOOP ("Volume")));
    v.push_back (knob (170, 77, 46, "Volume", nullptr, 0, noCaption));
    v.push_back (value (130, 112, 80, 20, "Volume", 0));
    return l;
}

Layout fruitySend()
{
    // one row: DRY, the SEND TO box (the destination insert, a menu of the mixer's inserts), PAN and VOLUME
    // (FL Studio 2026, 263 × 89; REA_rodada105_send §4)
    Layout l;
    l.fl = "Fruity Send";
    l.width = 263;
    l.height = 89;
    auto& v = l.items;
    v.push_back (section (0, 0, 263, 89, nullptr, boxed));
    v.push_back (knob (37, 30, 46, "Dry", TR_NOOP ("Dry"), 72));
    LayoutItem to;
    to.type = Item::menu;
    to.x = 78, to.y = 17, to.w = 48, to.h = 28;
    to.param = "Send to";
    to.label = TR_NOOP ("Send to");
    to.options = TR_NOOP ("None"); // the editor lists the song's inserts
    v.push_back (to);
    v.push_back (label (70, 52, 64, 14, TR_NOOP ("Send to")));
    v.push_back (knob (158, 30, 46, "Pan", TR_NOOP ("Pan"), 72, bipolar));
    v.push_back (knob (226, 30, 46, "Volume", TR_NOOP ("Volume"), 72));
    return l;
}

Layout lynxTune()
{
    // LYNX Tune (own design, REA round 95; not an FL plugin): PITCH (key, scale, voice range, A4 reference) ·
    // CORRECTION (retune — the main control —, humanize, tracking) · FORMANT (preserve, shift) · OUTPUT (transpose,
    // mix, low latency, MIDI source); the twelve target-note switches along the bottom, like a keyboard octave
    Layout l;
    l.width = 600;
    l.height = 236;
    auto& v = l.items;
    v.push_back (section (0, 0, 190, 160, TR_NOOP ("PITCH")));
    LayoutItem key;
    key.type = Item::menu;
    key.x = 10, key.y = 30, key.w = 80, key.h = 22;
    key.param = "Key";
    key.label = TR_NOOP ("Key");
    key.options = "C|C#|D|D#|E|F|F#|G|G#|A|A#|B";
    v.push_back (key);
    LayoutItem scale;
    scale.type = Item::menu;
    scale.x = 98, scale.y = 30, scale.w = 84, scale.h = 22;
    scale.param = "Scale";
    scale.label = TR_NOOP ("Scale");
    scale.options = TR_NOOP ("Chromatic|Major|Minor|Harmonic minor|Melodic minor|Major pentatonic|Minor pentatonic|Blues|Dorian|Mixolydian|Phrygian|Lydian|Locrian|Custom");
    v.push_back (scale);
    v.push_back (radio (10, 62, 96, 88, "Range", TR_NOOP ("Soprano|Alto / Tenor|Low male|Instrument")));
    v.push_back (knob (148, 100, 40, "Reference", TR_NOOP ("A4"), 136, showValue));
    v.push_back (section (194, 0, 210, 160, TR_NOOP ("CORRECTION")));
    v.push_back (knob (250, 84, 72, "Retune", TR_NOOP ("RETUNE"), 140, big | showValue));
    v.push_back (knob (334, 64, 38, "Humanize", TR_NOOP ("HUMANIZE"), 98, showValue));
    v.push_back (knob (334, 122, 30, "Tracking", TR_NOOP ("TRACKING"), 150, 0));
    v.push_back (section (408, 0, 90, 160, TR_NOOP ("FORMANT")));
    v.push_back (toggle (416, 30, 76, 20, "Formant preserve", TR_NOOP ("Preserve")));
    v.push_back (knob (453, 96, 40, "Formant shift", TR_NOOP ("SHIFT"), 136, bipolar | showValue));
    v.push_back (section (502, 0, 98, 160, TR_NOOP ("OUTPUT")));
    v.push_back (stepper (510, 30, 82, 22, "Transpose", TR_NOOP ("Transpose"), 25));
    v.push_back (knob (551, 92, 34, "Mix", TR_NOOP ("MIX"), 124, showValue));
    v.push_back (toggle (510, 136, 82, 18, "Low latency", TR_NOOP ("Low latency")));
    v.push_back (section (0, 164, 600, 72, TR_NOOP ("TARGET NOTES")));
    static const char* const notes[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int k = 0; k < 12; ++k)
        v.push_back (toggle (8.0f + 40.0f * (float) k, 194, 36, 22, notes[k], notes[k]));
    v.push_back (stepper (500, 192, 92, 24, "MIDI", TR_NOOP ("MIDI channel"), 129));
    return l;
}

Layout fruitySqueeze()
{
    // SQUARIZE (amount), PUNCHER (preserve, impact, relation, amount) on plates; FILTER (on, type, freq, res, pre / post);
    // MIX and GAIN faders
    Layout l;
    l.fl = "Fruity Squeeze";
    l.width = 282;
    l.height = 223;
    auto& v = l.items;
    v.push_back (section (2, 2, 105, 52, nullptr, boxed));
    v.push_back (label (10, 6, 90, 16, TR_NOOP ("Squarize"), alignLeft));
    const auto row = [&v] (float y, const char* param, const char* caption) {
        v.push_back (label (13, y - 9, 60, 17, caption, boxed));
        v.push_back (knob (87, y, 24, param, nullptr, 0, noCaption));
    };
    row (39, "Steps", TR_NOOP ("Amount"));
    v.push_back (section (2, 58, 105, 162, nullptr, boxed));
    v.push_back (label (10, 62, 90, 16, TR_NOOP ("Puncher"), alignLeft));
    row (100, "Preserve", TR_NOOP ("Preserve"));
    row (133, "Impact", TR_NOOP ("Impact"));
    row (165, "Relation", TR_NOOP ("Relation"));
    row (198, "Amount", TR_NOOP ("Amount"));
    v.push_back (section (110, 2, 100, 164, nullptr, boxed));
    v.push_back (toggle (116, 6, 90, 16, "Filter", TR_NOOP ("Filter")));
    v.push_back (section (116, 30, 88, 19, nullptr, boxed | raised));
    v.push_back (value (116, 30, 88, 19, "Filter type", 0));
    v.push_back (label (116, 66, 40, 14, TR_NOOP ("Freq.")));
    v.push_back (knob (136, 102, 34, "Cutoff", nullptr, 0, noCaption));
    v.push_back (label (162, 66, 40, 14, TR_NOOP ("Res.")));
    v.push_back (knob (182, 102, 34, "Resonance", nullptr, 0, noCaption));
    v.push_back (tabs (117, 137, 88, 18, "Filter post", TR_NOOP ("Pre|Pos")));
    v.push_back (section (213, 2, 67, 164, nullptr, boxed));
    v.push_back (slider (Item::vslider, 219, 12, 20, 148, "Mix", TR_NOOP ("Mix")));
    v.push_back (slider (Item::vslider, 251, 12, 20, 148, "Volume", TR_NOOP ("Gain")));
    v.push_back (label (112, 170, 168, 48, "Fruity Squeeze", dim));
    return l;
}

Layout fruityDelay2()
{
    // INPUT (pan, volume) | FEEDBACK (normal / invert / ping-pong, volume, cutoff) | TIME (time, offset) | DRY (volume)
    Layout l;
    l.fl = "Fruity Delay 2";
    l.width = 404;
    l.height = 102;
    auto& v = l.items;
    v.push_back (section (2, 2, 99, 74, TR_NOOP ("Input")));
    v.push_back (knob (28, 45, 33, "Input pan", TR_NOOP ("Pan"), 68, bipolar));
    v.push_back (knob (73, 45, 33, "Input volume", TR_NOOP ("Vol"), 68));
    v.push_back (section (103, 2, 148, 74, TR_NOOP ("Feedback")));
    v.push_back (radio (109, 24, 52, 46, "Mode", TR_NOOP ("Normal|Invert|P.pong")));
    v.push_back (knob (183, 45, 33, "Feedback", TR_NOOP ("Vol"), 68));
    v.push_back (knob (223, 45, 33, "Cutoff", TR_NOOP ("Cut"), 68));
    v.push_back (section (253, 2, 97, 74, TR_NOOP ("Time")));
    v.push_back (knob (279, 45, 33, "Time", TR_NOOP ("Time"), 68));
    v.push_back (knob (325, 45, 33, "Stereo offset", TR_NOOP ("Ofs"), 68, bipolar));
    v.push_back (section (352, 2, 50, 74, TR_NOOP ("Dry")));
    v.push_back (knob (378, 45, 33, "Dry", TR_NOOP ("Vol"), 68));
    v.push_back (label (104, 78, 296, 22, "Fruity Delay 2", dim));
    return l;
}

// --- generators (the channel's plugin page): items name the knobs of ChannelSettings' model panel ------------

Layout threeOsc()
{
    // three oscillator rows: shape buttons (sine, square, rounded, custom / triangle, saw, noise) and the invert switch,
    // PHASE OFS and DETUNE faders, COARSE, FINE, pan and (oscillators 2 and 3) volume; AM OSC 3, HQ and PHASE RAND below
    Layout l;
    l.fl = "3x Osc";
    l.width = 560;
    l.height = 390;
    auto& v = l.items;
    v.push_back (section (0, 0, 560, 310, nullptr, boxed));
    static const char* const names[3][7] = { { "Osc 1 shape", "Osc 1 phase", "Osc 1 detune", "Osc 1 coarse", "Osc 1 fine", "Osc 1 pan", nullptr },
                                             { "Osc 2 shape", "Osc 2 phase", "Osc 2 detune", "Osc 2 coarse", "Osc 2 fine", "Osc 2 pan", "Osc 2 mix" },
                                             { "Osc 3 shape", "Osc 3 phase", "Osc 3 detune", "Osc 3 coarse", "Osc 3 fine", "Osc 3 pan", "Osc 3 mix" } };
    static const char* const inverts[3] = { "Invert 1", "Invert 2", "Invert 3" };
    static const char* const numbers[3] = { "1", "2", "3" };
    for (int o = 0; o < 3; ++o)
    {
        const auto dy = 107.5f * (float) o;
        const auto captions = o < 2; // FL writes the captions between the rows
        v.push_back (label (2, 20 + dy, 30, 36, numbers[o], dim));
        // shape buttons in FL's order; the values are the engine's shapes 0…6 over 0..1
        auto shape = radio (37, 9 + dy, 152, 76, names[o][0], "Sine|Square|Rounded|Custom|Triangle|Saw|Noise",
                            { 0.0f, 2.0f / 6.0f, 4.0f / 6.0f, 1.0f, 1.0f / 6.0f, 3.0f / 6.0f, 5.0f / 6.0f });
        shape.columns = 4;
        v.push_back (shape);
        v.push_back (toggle (153, 52 + dy, 32, 28, inverts[o], "+/-", boxed));
        v.push_back (slider (Item::vslider, 222, 6 + dy, 20, 82, names[o][1], nullptr, noCaption | bipolar));
        v.push_back (slider (Item::vslider, 252, 6 + dy, 20, 82, names[o][2], nullptr, noCaption | bipolar));
        if (captions)
        {
            v.push_back (label (190, 95 + dy, 58, 14, TR_NOOP ("Phase ofs"), dim));
            v.push_back (label (250, 95 + dy, 40, 14, TR_NOOP ("Detune"), dim));
        }
        v.push_back (knob (338, 48 + dy, 46, names[o][3], captions ? TR_NOOP ("Coarse") : nullptr, 102 + dy, captions ? 0 : noCaption));
        v.push_back (knob (401, 48 + dy, 34, names[o][4], captions ? TR_NOOP ("Fine") : nullptr, 102 + dy, captions ? 0 : noCaption));
        v.push_back (knob (465, 48 + dy, 34, names[o][5], captions ? TR_NOOP ("Pan") : nullptr, 102 + dy, bipolar | (captions ? 0 : noCaption)));
        if (names[o][6] != nullptr)
            v.push_back (knob (523, 48 + dy, 46, names[o][6], o == 1 ? TR_NOOP ("Vol") : nullptr, 102 + dy, o == 1 ? 0 : noCaption));
    }
    v.push_back (section (0, 312, 560, 78, nullptr, boxed | raised));
    v.push_back (toggle (14, 342, 72, 16, "Osc 3 AM", TR_NOOP ("AM osc 3")));
    v.push_back (toggle (94, 342, 44, 16, "HQ", TR_NOOP ("HQ")));
    v.push_back (knob (165, 350, 44, "Phase rand", nullptr, 0, noCaption));
    v.push_back (label (192, 342, 80, 16, TR_NOOP ("Phase rand"), alignLeft));
    v.push_back (label (380, 326, 170, 50, "3x Osc", dim));
    return l;
}

Layout flKeys()
{
    // ENVIRONMENT, MISC, VELOCITY and TUNING over two rows of knobs; the sample set's name at the top right
    Layout l;
    l.fl = "FL Keys";
    l.width = 480;
    l.height = 186;
    auto& v = l.items;
    v.push_back (label (8, 2, 200, 34, "FL Keys", dim | alignLeft));
    v.push_back (display (333, 9, 141, 18, "flkeysname"));
    v.push_back (section (12, 37, 110, 145, TR_NOOP ("Environment")));
    v.push_back (section (132, 37, 111, 145, TR_NOOP ("Misc")));
    v.push_back (section (253, 37, 159, 145, TR_NOOP ("Velocity")));
    v.push_back (section (422, 37, 52, 145, TR_NOOP ("Tuning")));
    v.push_back (knob (37, 87, 34, "Decay", TR_NOOP ("Decay"), 110, bipolar));
    v.push_back (knob (95, 87, 34, "Release", TR_NOOP ("Release"), 110, bipolar));
    v.push_back (knob (37, 149, 34, "Pan/Trem", TR_NOOP ("Pan"), 172, bipolar));
    v.push_back (knob (95, 149, 34, "Stereo", TR_NOOP ("Stereo"), 172));
    v.push_back (knob (158, 87, 34, "Overdrive", TR_NOOP ("Overdrive"), 110));
    v.push_back (knob (216, 87, 34, "LFO rate", TR_NOOP ("LFO"), 110));
    v.push_back (knob (158, 149, 34, "Treble", TR_NOOP ("Treble"), 172, bipolar));
    v.push_back (knob (216, 149, 34, "Stretch", TR_NOOP ("Stretch"), 172, bipolar));
    v.push_back (knob (278, 87, 34, "Vel > Muffle", TR_NOOP ("Vel"), 110));
    v.push_back (knob (336, 87, 34, "Muffle", TR_NOOP ("Muffle"), 110));
    v.push_back (knob (278, 149, 34, "Vel > Hard", TR_NOOP ("Vel"), 172, bipolar));
    v.push_back (knob (336, 149, 34, "Hardness", TR_NOOP ("Hardness"), 172, bipolar));
    v.push_back (knob (394, 149, 34, "Vel sens", TR_NOOP ("Sens"), 172, bipolar));
    v.push_back (knob (447, 87, 34, "Tune", TR_NOOP ("Tune"), 110, bipolar));
    v.push_back (knob (447, 149, 34, "Detune", TR_NOOP ("Detune"), 172));
    return l;
}

Layout booBass()
{
    // BASS, MID and TREBLE stepping down along the instrument's body
    Layout l;
    l.fl = "BooBass";
    l.width = 343;
    l.height = 154;
    auto& v = l.items;
    v.push_back (label (180, 0, 160, 34, "BooBass", dim));
    v.push_back (section (40, 36, 300, 116, nullptr, boxed));
    v.push_back (knob (79, 74, 40, "Bass", TR_NOOP ("Bass"), 107));
    v.push_back (knob (151, 92, 40, "Mid", TR_NOOP ("Mid"), 128));
    v.push_back (knob (222, 102, 40, "Treble", TR_NOOP ("Treble"), 138));
    return l;
}

Layout sytrusMain()
{
    // Sytrus' MAIN page: master faders, volume and filter envelopes, unison, the modulation X/Y pad, quality and porta.
    // The operator, filter and effect pages and the FM / RM matrix keep the saved patch (not editable in LYNX).
    Layout l;
    l.fl = "Sytrus";
    l.width = 850;
    l.height = 346;
    auto& v = l.items;
    v.push_back (label (8, 4, 80, 22, TR_NOOP ("Main"), boxed));
    const auto fader = [&v] (float cx, const char* param, const char* caption, bool bipolarFader) {
        v.push_back (slider (Item::vslider, cx - 9, 54, 18, 70, param, nullptr, noCaption | (bipolarFader ? bipolar : 0)));
        v.push_back (label (cx - 16, 38, 32, 13, caption));
    };
    v.push_back (section (12, 34, 136, 98, nullptr, boxed));
    fader (38, "Master level", TR_NOOP ("Vol"), false);
    fader (81, "Master LFO", TR_NOOP ("LFO"), true);
    fader (124, "Master pitch", TR_NOOP ("Pitch"), true);
    v.push_back (section (152, 34, 98, 98, nullptr, boxed));
    fader (165, "Vol attack", "A", false);
    fader (189, "Vol decay", "D", false);
    fader (213, "Vol sustain", "S", true);
    fader (237, "Vol release", "R", false);
    v.push_back (label (156, 122, 80, 10, TR_NOOP ("Vol"), dim | alignLeft));
    v.push_back (section (256, 34, 98, 98, nullptr, boxed));
    fader (269, "Flt attack", "A", false);
    fader (293, "Flt decay", "D", false);
    fader (317, "Flt sustain", "S", true);
    fader (341, "Flt release", "R", false);
    v.push_back (label (260, 122, 80, 10, TR_NOOP ("Filter"), dim | alignLeft));
    v.push_back (section (360, 34, 180, 98, nullptr, boxed));
    v.push_back (stepper (366, 38, 30, 34, "Unison", TR_NOOP ("Ord"), 8));
    fader (410, "Uni pan", TR_NOOP ("Pn"), false);
    fader (434, "Uni volume", TR_NOOP ("Vl"), false);
    fader (458, "Uni pitch", TR_NOOP ("Pt"), false);
    fader (481, "Uni sub", TR_NOOP ("Sb"), false);
    fader (504, "Uni phase", TR_NOOP ("Ph"), false);
    fader (527, "Uni env var", TR_NOOP ("En"), false);
    v.push_back (label (470, 122, 66, 10, TR_NOOP ("Unison"), dim));
    v.push_back (section (12, 138, 168, 160, TR_NOOP ("Modulation")));
    v.push_back (display (18, 160, 128, 130, "xy", "Mod X", "Mod Y"));
    v.push_back (knob (164, 185, 24, "Mod X", "X", 205));
    v.push_back (knob (164, 232, 24, "Mod Y", "Y", 252));
    v.push_back (section (362, 138, 178, 86, TR_NOOP ("Quality")));
    v.push_back (label (462, 156, 34, 12, TR_NOOP ("Draft")));
    v.push_back (label (500, 156, 34, 12, TR_NOOP ("Render")));
    v.push_back (label (368, 174, 92, 16, TR_NOOP ("HQ envelopes"), alignLeft));
    v.push_back (toggle (470, 175, 16, 14, "Live env accuracy", nullptr));
    v.push_back (toggle (508, 175, 16, 14, "Export env accuracy", nullptr));
    v.push_back (label (368, 198, 92, 16, TR_NOOP ("Oversampling"), alignLeft));
    v.push_back (stepper (462, 196, 32, 22, "Live oversampling", nullptr, 4));
    v.push_back (stepper (500, 196, 32, 22, "Export oversampling", nullptr, 4));
    v.push_back (toggle (450, 270, 70, 16, "Porta", TR_NOOP ("Porta")));
    v.push_back (section (553, 34, 294, 260, nullptr, boxed));
    v.push_back (label (560, 150, 280, 16, TR_NOOP ("Operators, filters, effects and the matrix"), dim));
    v.push_back (label (560, 168, 280, 16, TR_NOOP ("keep the saved patch"), dim));
    v.push_back (label (560, 300, 286, 40, "Sytrus", dim));
    return l;
}

Layout harmorMain()
{
    // Harmor's part page and global section, with the controls LYNX edits at FL's places (timbre mix, blur, tremolo,
    // harmonizer, unison order and detune, vibrato, filter cutoff and resonance, pluck, phaser, part volume, A/B
    // crossfade and the global volume); the rest of the panel keeps the saved patch
    Layout l;
    l.fl = "Harmor";
    l.width = 1000;
    l.height = 600;
    auto& v = l.items;
    v.push_back (section (12, 0, 976, 330, nullptr, boxed));
    const auto title = [&v] (float x, float y, const char* text) { v.push_back (label (x, y, 110, 18, text, alignLeft)); };
    title (100, 70, TR_NOOP ("Timbre"));
    title (18, 104, TR_NOOP ("Blur"));
    title (228, 104, TR_NOOP ("Trem"));
    title (60, 180, TR_NOOP ("Prism"));
    title (210, 180, TR_NOOP ("Harmonizer"));
    title (380, 160, TR_NOOP ("Unison"));
    title (40, 278, TR_NOOP ("Pitch"));
    title (270, 270, TR_NOOP ("Vibrato"));
    title (400, 270, TR_NOOP ("Legato"));
    title (555, 238, TR_NOOP ("Pluck"));
    title (655, 238, TR_NOOP ("Phaser"));
    title (900, 238, TR_NOOP ("EQ"));
    title (555, 20, TR_NOOP ("Filter 1"));
    v.push_back (knob (177, 40, 30, "Timbre mix", TR_NOOP ("Mix"), 63));
    v.push_back (knob (61, 135, 30, "Harm. blur", TR_NOOP ("Mix"), 160));
    v.push_back (knob (277, 135, 30, "Tremolo", TR_NOOP ("Depth"), 160));
    v.push_back (knob (181, 219, 34, "Harmonizer", TR_NOOP ("Amt"), 247));
    v.push_back (stepper (414, 196, 30, 22, "Unison", TR_NOOP ("Order"), 8));
    v.push_back (slider (Item::vslider, 476, 182, 18, 80, "Uni detune", TR_NOOP ("Pitch")));
    v.push_back (knob (335, 291, 28, "Vibrato", TR_NOOP ("Depth"), 312));
    v.push_back (slider (Item::vslider, 455, 40, 18, 100, "A volume", TR_NOOP ("Vol"), bipolar));
    v.push_back (knob (725, 71, 44, "Cutoff", TR_NOOP ("Freq"), 106));
    v.push_back (knob (828, 71, 34, "Resonance", TR_NOOP ("Res"), 106));
    v.push_back (knob (587, 283, 40, "Pluck", TR_NOOP ("Time"), 313));
    v.push_back (knob (686, 283, 34, "Phaser mix", TR_NOOP ("Mix"), 313));
    v.push_back (section (12, 334, 380, 266, nullptr, boxed));
    title (22, 342, TR_NOOP ("Global"));
    v.push_back (slider (Item::vslider, 172, 410, 20, 110, "Master vol", TR_NOOP ("Vol")));
    v.push_back (slider (Item::hslider, 280, 368, 90, 26, "A/B mix", TR_NOOP ("Part A / B")));
    v.push_back (section (396, 334, 592, 266, nullptr, boxed));
    v.push_back (label (420, 450, 540, 16, TR_NOOP ("The other Harmor controls keep the saved patch"), dim));
    v.push_back (label (820, 556, 160, 40, "Harmor", dim));
    return l;
}

Layout maximus()
{
    // FL Studio 2026 Maximus (measured on its window, body origin under the wrapper title): the selected band's
    // compression curve at the top-left and the bands view across the rest of the top; under them the band selector
    // (LOW / MID / HIGH / MASTER), the band's mode switch, PRE / POST gain, stereo separation, THRES / CEIL saturation
    // inset, the envelope (ATT, REL, SUSTAIN, the two CURVE steppers, PEAK / RMS, REL 2); LMH DEL / LMH MIX; the
    // crossover (LOW / HIGH frequency, 12 / 24 dB slopes) and LOW CUT. The values live in the slot's Maximus state
    // (core/MaximusFx.h stateParams: "Low mode", "Master pre gain", ...).
    Layout l;
    l.fl = "Maximus";
    l.width = 633;
    l.height = 391;
    l.pages = { TR_NOOP ("Low"), TR_NOOP ("Mid"), TR_NOOP ("High"), TR_NOOP ("Master") };
    auto& v = l.items;
    v.push_back (display (8, 4, 181, 181, "maxcurve"));
    v.push_back (display (192, 4, 433, 181, "maxbands"));
    v.push_back (section (2, 205, 248, 183, nullptr, boxed));
    v.push_back (section (252, 205, 176, 183, nullptr, boxed));
    v.push_back (section (430, 205, 67, 183, nullptr, boxed));
    v.push_back (section (499, 205, 132, 183, nullptr, boxed));
    auto bandSelector = pageTabs (4, 209, 70, 99, TR_NOOP ("Low|Mid|High|Master"));
    v.push_back (bandSelector);
    v.push_back (section (142, 294, 106, 74, nullptr, boxed | raised));

    static const char* bandNames[4] { "Low", "Mid", "High", "Master" };
    static std::vector<std::string> names; // "<Band> <control>" (core/MaximusFx.h), kept for the items' pointers
    if (names.empty())
    {
        static const char* controls[13] { "mode", "pre gain", "post gain", "stereo separation", "saturation threshold",
                                          "saturation ceiling", "attack", "release 2", "attack curve", "release",
                                          "release curve", "sustain", "envelope mode" };
        names.reserve (52);
        for (const auto* b : bandNames)
            for (const auto* c : controls)
                names.push_back (std::string (b) + " " + c);
    }
    for (int b = 0; b < 4; ++b)
    {
        auto n = [&] (int j) { return names[(std::size_t) (13 * b + j)].c_str(); };
        auto add = [&] (LayoutItem i) { i.colour = b; v.push_back (onPage (std::move (i), b)); };
        add (radio (81, 216, 64, 60, n (0), TR_NOOP ("On|Comp off|Muted|Off")));
        add (knob (169.5f, 243, 42, n (1), TR_NOOP ("Pre"), 272));
        add (knob (223, 243, 42, n (2), TR_NOOP ("Post"), 272));
        add (label (185, 266, 24, 13, TR_NOOP ("Gain"), boxed));
        add (knob (105.5f, 323, 38, n (3), TR_NOOP ("Sep"), 349, bipolar));
        add (knob (168.5f, 322, 36, n (4), TR_NOOP ("Thres"), 349, bipolar));
        add (knob (223, 322, 36, n (5), TR_NOOP ("Ceil"), 349));
        add (label (186, 343, 22, 13, TR_NOOP ("Sat"), boxed));
        add (knob (280.5f, 243, 42, n (6), TR_NOOP ("Att"), 272));
        add (knob (343.5f, 243, 42, n (9), TR_NOOP ("Rel"), 272));
        add (knob (395.5f, 243, 42, n (11), TR_NOOP ("Sustain"), 272));
        add (label (300, 266, 24, 13, TR_NOOP ("Env"), boxed));
        add (stepper (268, 292, 27, 22, n (8), nullptr, 7));
        add (label (296, 297, 34, 13, TR_NOOP ("Curve")));
        add (stepper (331, 292, 27, 22, n (10), nullptr, 7));
        add (radio (368, 292, 46, 30, n (12), TR_NOOP ("Peak|RMS")));
        add (knob (281, 353, 34, n (7), TR_NOOP ("Rel 2"), 377));
    }
    v.push_back (knob (463, 243, 42, "LMH lookahead delay", TR_NOOP ("LMH del"), 272));
    v.push_back (knob (463, 323, 42, "LMH mix", TR_NOOP ("LMH mix"), 349));
    v.push_back (knob (534, 243, 42, "Low band frequency", TR_NOOP ("Low"), 272));
    v.push_back (knob (593, 243, 42, "High band frequency", TR_NOOP ("High"), 272));
    v.push_back (label (551, 266, 25, 13, TR_NOOP ("Freq"), boxed));
    v.push_back (radio (503, 292, 42, 28, "Low band slope", "12dB|24dB"));
    v.push_back (radio (584, 292, 42, 28, "High band slope", "12dB|24dB"));
    v.push_back (knob (538, 349, 34, "Low cut", TR_NOOP ("Low cut"), 377));
    return l;
}

Layout vintagePhaser()
{
    // PHASING (MIN, MAX faders, HQ, MANUAL, FEEDBACK fader) | MODULATION (START PHASE, TYPE switch, L/R PHASE,
    // TENSION, SPEED fader with T) | DELAY (on LED; MIX, STEREO, big TIME with T; FEEDBACK, LP, HP, KEEP PITCH) |
    // LEVELS (NOISE, NOISE GATE, MONO INPUT, MIX fader). The engine's remaining controls (FL keeps them out of the
    // panel; where FL shows them is not confirmed) are on a second page.
    Layout l;
    l.fl = "Vintage Phaser";
    l.width = 754;
    l.height = 214;
    l.pages = { TR_NOOP ("Main"), TR_NOOP ("Advanced") };
    auto& v = l.items;
    v.push_back (pageTabs (13, 14, 120, 20, TR_NOOP ("Main|Advanced")));
    v.push_back (label (420, 6, 326, 34, "Vintage Phaser", dim));
    auto main = [&] (LayoutItem i) { v.push_back (onPage (std::move (i), 0)); };
    auto fader = [&] (float x, const char* param, const char* caption) {
        main (slider (Item::vslider, x - 10, 72, 20, 104, param, nullptr, noCaption));
        main (label (x - 24, 180, 48, 12, caption));
    };
    main (section (13, 45, 162, 165, TR_NOOP ("Phasing")));
    fader (38, "Min freq.", TR_NOOP ("Min"));
    fader (71, "Max freq.", TR_NOOP ("Max"));
    main (toggle (95, 132, 20, 12, "HQ", nullptr));
    main (label (88, 146, 34, 11, TR_NOOP ("HQ")));
    main (toggle (95, 164, 20, 12, "Manual", nullptr));
    main (label (82, 180, 46, 12, TR_NOOP ("Manual")));
    fader (143, "Feedback", TR_NOOP ("Feedback"));

    main (section (181, 45, 209, 165, TR_NOOP ("Modulation")));
    main (knob (220, 92, 30, "Start phase", TR_NOOP ("Start phase"), 124));
    main (radio (256, 76, 52, 30, "Sine", TR_NOOP ("Triangle|Sine")));
    main (label (252, 112, 50, 12, TR_NOOP ("Type")));
    main (knob (220, 157, 30, "L/R phase", TR_NOOP ("L/R phase"), 186));
    main (knob (281, 157, 30, "Tension", TR_NOOP ("Tension"), 186));
    fader (348, "LFO speed", TR_NOOP ("Speed"));
    main (toggle (364, 180, 14, 12, "Tempo sync", "T", boxed));

    main (section (396, 45, 217, 165, TR_NOOP ("Delay")));
    main (toggle (582, 49, 20, 10, "Delay", nullptr));
    main (knob (428, 92, 30, "Delay mix", TR_NOOP ("Mix"), 124));
    main (knob (472, 92, 30, "Delay stereo", TR_NOOP ("Stereo"), 124, bipolar));
    main (knob (548, 100, 44, "Delay time", TR_NOOP ("Time"), 136, big));
    main (toggle (576, 130, 14, 12, "Delay sync", "T", boxed));
    main (knob (428, 157, 30, "Delay fdbk", TR_NOOP ("Feedback"), 186));
    main (knob (472, 157, 30, "Delay LP", TR_NOOP ("LP"), 186));
    main (knob (527, 157, 30, "Delay HP", TR_NOOP ("HP"), 186));
    main (toggle (562, 164, 20, 12, "Keep pitch", nullptr));
    main (label (550, 180, 56, 12, TR_NOOP ("Keep pitch")));

    main (section (619, 45, 119, 165, TR_NOOP ("Levels")));
    main (knob (654, 90, 28, "Noise", TR_NOOP ("Noise"), 120));
    main (toggle (644, 134, 20, 12, "Gate", nullptr));
    main (label (626, 148, 56, 11, TR_NOOP ("Noise gate")));
    main (toggle (644, 164, 20, 12, "Mono input", nullptr));
    main (label (626, 180, 56, 12, TR_NOOP ("Mono input")));
    fader (708, "Mix", TR_NOOP ("Mix"));

    // second page: the engine's other controls
    auto adv = [&] (int k, const char* param, const char* caption, std::uint16_t style = 0) {
        const auto cx = 50.0f + 92.0f * (float) (k % 8), cy = 82.0f + 70.0f * (float) (k / 8);
        v.push_back (onPage (knob (cx, cy, 34, param, caption, cy + 26, style | showValue), 1));
    };
    v.push_back (onPage (section (13, 45, 725, 165, nullptr, boxed), 1));
    adv (0, "Manual freq.", TR_NOOP ("Manual freq."));
    adv (1, "Rate 1", TR_NOOP ("Rate 1"));
    adv (2, "Rate 2", TR_NOOP ("Rate 2"));
    adv (3, "Depth scale", TR_NOOP ("Depth scale"));
    adv (4, "Pull-down", TR_NOOP ("Pull-down"));
    adv (5, "Drive", TR_NOOP ("Drive"), bipolar);
    adv (6, "Colour gain", TR_NOOP ("Colour gain"));
    adv (7, "Colour freq.", TR_NOOP ("Colour freq."));
    adv (8, "Input HP", TR_NOOP ("Input HP"));
    adv (9, "Fdbk HP", TR_NOOP ("Feedback HP"));
    v.push_back (onPage (toggle (230, 140, 110, 14, "Engine V2", TR_NOOP ("Engine V2")), 1));
    v.push_back (onPage (toggle (230, 166, 110, 14, "Invert fdbk", TR_NOOP ("Invert feedback")), 1));
    return l;
}

Layout soundgoodizer()
{
    // one big Amount knob and the four presets A–D (the slot's state, core/MaximusFx.h)
    Layout l;
    l.fl = "Soundgoodizer";
    l.width = 215;
    l.height = 300;
    auto& v = l.items;
    v.push_back (label (10, 6, 195, 36, "Soundgoodizer", dim));
    v.push_back (display (28, 64, 152, 152, "sgamount"));
    v.push_back (display (14, 228, 186, 50, "sgpresets"));
    return l;
}

// --- generated layouts ------------------------------------------------------------------------------

// --- LYNX originals: the mixing suite (REA round 96). Not FL arrangements: LYNX's own panels, laid out with the
// same conventions (titled sections, knobs with captions and read-outs, the graph on top, switches as tabs). -------

// a knob with its caption and its value under it (`d` wide, centred at cx, cy)
LayoutItem dial (float cx, float cy, float d, const char* param, const char* caption, std::uint16_t style = 0)
{
    return knob (cx, cy, d, param, caption, cy + d * 0.5f + 20.0f, (std::uint16_t) (style | showValue));
}

// a live meter of the effect's control output `output` (0 = gain reduction, 1 = second gain reduction)
LayoutItem meter (float x, float y, float w, float h, int output, const char* caption)
{
    auto i = display (x, y, w, h, "grmeter");
    i.steps = output;
    i.label = caption;
    return i;
}

Layout ridgeStrip()
{
    Layout l;
    l.width = 700;
    l.height = 360;
    auto& v = l.items;
    // filters
    v.push_back (section (4, 4, 104, 352, TR_NOOP ("Filters")));
    v.push_back (dial (56, 62, 40, "HPF", TR_NOOP ("High-pass")));
    v.push_back (dial (56, 150, 40, "LPF", TR_NOOP ("Low-pass")));
    v.push_back (toggle (12, 214, 92, 20, "Filters to SC", TR_NOOP ("To sidechain")));

    // equalizer: the curve on top, one column per band (LF … HF, low to high as on the graph)
    v.push_back (section (112, 4, 340, 352, TR_NOOP ("Equalizer")));
    v.push_back (display (118, 24, 328, 96, "ridgeeq"));
    const char* bands[] = { TR_NOOP ("LF"), TR_NOOP ("LMF"), TR_NOOP ("HMF"), TR_NOOP ("HF") };
    const char* gains[] = { "LF gain", "LMF gain", "HMF gain", "HF gain" };
    const char* freqs[] = { "LF freq.", "LMF freq.", "HMF freq.", "HF freq." };
    for (int b = 0; b < 4; ++b)
    {
        const auto cx = 158.0f + 82.0f * (float) b;
        v.push_back (label (cx - 36.0f, 124, 72, 14, bands[b], boxed));
        v.push_back (dial (cx, 162, 38, gains[b], TR_NOOP ("Gain"), bipolar));
        v.push_back (dial (cx, 230, 32, freqs[b], TR_NOOP ("Freq")));
        if (b == 1 || b == 2)
            v.push_back (dial (cx, 290, 28, b == 1 ? "LMF Q" : "HMF Q", TR_NOOP ("Q")));
        else
            v.push_back (toggle (cx - 30.0f, 282, 62, 20, b == 0 ? "LF bell" : "HF bell", TR_NOOP ("Bell")));
    }
    v.push_back (tabs (118, 330, 130, 20, "EQ curve", TR_NOOP ("Brown|Black")));
    v.push_back (toggle (360, 330, 88, 20, "EQ on", TR_NOOP ("EQ in")));

    // dynamics: static curve, the two gain-reduction meters, compressor and gate
    v.push_back (section (456, 4, 160, 352, TR_NOOP ("Dynamics")));
    v.push_back (display (462, 24, 148, 66, "ridgecomp"));
    v.push_back (meter (462, 94, 148, 10, 0, TR_NOOP ("Comp")));
    v.push_back (meter (462, 108, 148, 10, 1, TR_NOOP ("Gate")));
    v.push_back (label (462, 122, 148, 14, TR_NOOP ("Compressor"), boxed));
    v.push_back (dial (488, 158, 30, "Ratio", TR_NOOP ("Ratio")));
    v.push_back (dial (536, 158, 30, "Comp. threshold", TR_NOOP ("Thres")));
    v.push_back (dial (584, 158, 30, "Comp. release", TR_NOOP ("Release")));
    v.push_back (dial (488, 214, 24, "Comp. mix", TR_NOOP ("Mix")));
    v.push_back (toggle (512, 208, 100, 20, "Fast attack", TR_NOOP ("Fast attack")));
    v.push_back (label (462, 252, 148, 14, TR_NOOP ("Gate / expander"), boxed));
    v.push_back (dial (488, 286, 30, "Gate range", TR_NOOP ("Range")));
    v.push_back (dial (536, 286, 30, "Gate threshold", TR_NOOP ("Thres")));
    v.push_back (dial (584, 286, 30, "Gate release", TR_NOOP ("Release")));
    v.push_back (toggle (462, 334, 70, 20, "Gate fast", TR_NOOP ("Fast")));
    v.push_back (toggle (536, 334, 76, 20, "Expander", TR_NOOP ("Expand")));

    // master: trims, order, phase, dynamics in
    v.push_back (section (620, 4, 76, 352, TR_NOOP ("Master")));
    v.push_back (dial (658, 58, 34, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (658, 134, 34, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (tabs (628, 190, 60, 76, "Order", TR_NOOP ("EQ>Dyn|Dyn>EQ")));
    v.push_back (toggle (626, 278, 68, 20, "Dynamics on", TR_NOOP ("Dyn in")));
    v.push_back (toggle (626, 306, 68, 20, "Phase", TR_NOOP ("Phase")));
    return l;
}

Layout glowLeveler()
{
    Layout l;
    l.width = 420;
    l.height = 220;
    auto& v = l.items;
    v.push_back (section (4, 4, 412, 212, TR_NOOP ("Optical leveler")));
    v.push_back (meter (16, 28, 388, 20, 0, TR_NOOP ("Gain reduction")));
    v.push_back (dial (90, 112, 64, "Gain", TR_NOOP ("Gain"), big));
    v.push_back (dial (330, 112, 64, "Peak reduction", TR_NOOP ("Peak reduction"), big));
    v.push_back (tabs (166, 62, 88, 22, "Limit", TR_NOOP ("Compress|Limit")));
    v.push_back (dial (186, 128, 32, "Emphasis", TR_NOOP ("Emphasis")));
    v.push_back (dial (236, 128, 32, "Mix", TR_NOOP ("Mix")));
    return l;
}

Layout hushDeEsser()
{
    Layout l;
    l.width = 460;
    l.height = 200;
    auto& v = l.items;
    v.push_back (section (4, 4, 452, 192, TR_NOOP ("De-esser")));
    v.push_back (display (12, 26, 200, 78, "hushband"));
    v.push_back (meter (12, 110, 200, 12, 0, TR_NOOP ("Reduction")));
    v.push_back (tabs (12, 130, 98, 20, "Wideband", TR_NOOP ("Split|Wide")));
    v.push_back (tabs (114, 130, 98, 20, "Band detector", TR_NOOP ("High|Band")));
    v.push_back (toggle (12, 162, 84, 20, "Listen", TR_NOOP ("Listen")));
    v.push_back (toggle (100, 162, 112, 20, "Lookahead", TR_NOOP ("Lookahead")));
    v.push_back (dial (262, 58, 46, "Frequency", TR_NOOP ("Frequency"), big));
    v.push_back (dial (338, 58, 46, "Threshold", TR_NOOP ("Threshold"), big));
    v.push_back (dial (412, 58, 40, "Range", TR_NOOP ("Range")));
    v.push_back (dial (300, 142, 32, "Release", TR_NOOP ("Release")));
    v.push_back (dial (376, 142, 32, "Output", TR_NOOP ("Output"), bipolar));
    return l;
}

Layout aetherLayout()
{
    Layout l;
    l.width = 380;
    l.height = 262;
    auto& v = l.items;
    v.push_back (section (4, 4, 182, 192, TR_NOOP ("Air")));
    v.push_back (dial (95, 68, 58, "Air", TR_NOOP ("Air"), big));
    v.push_back (dial (95, 150, 32, "Air freq.", TR_NOOP ("Freq")));
    v.push_back (section (190, 4, 186, 192, TR_NOOP ("Exciter")));
    v.push_back (dial (283, 68, 58, "Harmonics", TR_NOOP ("Harmonics"), big));
    v.push_back (dial (236, 150, 32, "Exciter freq.", TR_NOOP ("Freq")));
    v.push_back (dial (330, 150, 32, "Character", TR_NOOP ("Even / odd"), bipolar));
    v.push_back (section (4, 200, 372, 58, nullptr, boxed));
    v.push_back (dial (140, 218, 24, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (240, 218, 24, "Output", TR_NOOP ("Output"), bipolar));
    return l;
}

Layout emberLayout()
{
    Layout l;
    l.width = 520;
    l.height = 230;
    auto& v = l.items;
    v.push_back (section (4, 4, 200, 222, TR_NOOP ("Style")));
    v.push_back (display (12, 26, 184, 110, "embercurve"));
    auto styles = radio (12, 144, 184, 76, "Style", TR_NOOP ("Tube|Tape|Transistor|Fuzz"));
    styles.columns = 2;
    v.push_back (styles);
    v.push_back (section (208, 4, 308, 222, TR_NOOP ("Drive")));
    v.push_back (dial (266, 86, 70, "Drive", TR_NOOP ("Drive"), big));
    v.push_back (dial (380, 60, 36, "Tone", TR_NOOP ("Tone"), bipolar));
    v.push_back (dial (460, 60, 36, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (340, 148, 30, "Low cut", TR_NOOP ("Low cut")));
    v.push_back (dial (410, 148, 30, "High cut", TR_NOOP ("High cut")));
    v.push_back (dial (480, 148, 30, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (toggle (216, 200, 104, 20, "Auto gain", TR_NOOP ("Auto gain")));
    v.push_back (tabs (340, 200, 168, 20, "Oversampling", TR_NOOP ("2x|4x|8x"), { 0.0f, 0.5f, 1.0f }));
    return l;
}

Layout generated (EffectKind kind)
{
    // knobs in rows of up to 6 (FL's wrapper shows unknown plugins as a list of knobs too)
    const auto& info = effectInfo (kind);
    Layout l;
    const auto count = (int) info.params.size();
    const auto columns = count <= 4 ? std::max (1, count) : count <= 12 ? 6 : 8;
    const auto rows = (count + columns - 1) / columns;
    constexpr float cellW = 74.0f, cellH = 84.0f;
    l.width = (int) (columns * cellW + 12.0f);
    l.height = (int) std::max (60.0f, (float) rows * cellH + 12.0f);

    if (count > 0)
        l.items.push_back (section (4, 4, (float) l.width - 8.0f, (float) l.height - 8.0f, nullptr, boxed));
    for (int i = 0; i < count; ++i)
    {
        const auto cx = 6.0f + cellW * ((float) (i % columns) + 0.5f);
        const auto cy = 6.0f + cellH * (float) (i / columns) + 26.0f;
        l.items.push_back (knob (cx, cy, 36, info.params[(std::size_t) i].name, info.params[(std::size_t) i].name, cy + 30.0f, showValue));
    }
    if (count == 0)
        l.items.push_back (display (6, 6, (float) l.width - 12.0f, (float) l.height - 12.0f, "name"));
    return l;
}
} // namespace

const std::vector<EffectKind>& flLayoutKinds()
{
    static const std::vector<EffectKind> kinds { EffectKind::fruityLimiter, EffectKind::fruityDelay3, EffectKind::fruityMultiband,
                                                 EffectKind::reeverb2, EffectKind::luxeVerb, EffectKind::fruityCompressor,
                                                 EffectKind::fruityFilter, EffectKind::fruityFastDist, EffectKind::fruityChorus,
                                                 EffectKind::fruityFlanger, EffectKind::stereoEnhancer, EffectKind::parametricEq,
                                                 EffectKind::grossBeat, EffectKind::fruityDelay2, EffectKind::reeverb1,
                                                 EffectKind::fruityFlangus, EffectKind::fruitySoftClipper,
                                                 EffectKind::fruityBloodOverdrive, EffectKind::fruityPanOMatic, EffectKind::stereoShaper,
                                                 EffectKind::transientProcessor, EffectKind::fruityFreeFilter, EffectKind::fruityWaveShaper,
                                                 EffectKind::vintageChorus, EffectKind::hyperChorus, EffectKind::fruityBalance,
                                                 EffectKind::squeeze, EffectKind::maximus,
                                                 EffectKind::vintagePhaser, EffectKind::fruitySend };
    return kinds;
}

const std::vector<EffectKind>& ownLayoutKinds()
{
    static const std::vector<EffectKind> kinds { EffectKind::ridgeStrip, EffectKind::glowLeveler, EffectKind::hushDeEsser,
                                                 EffectKind::aether, EffectKind::ember };
    return kinds;
}

const Layout& layoutFor (EffectKind kind)
{
    static std::mutex lock;
    static std::map<EffectKind, Layout> cache;
    const std::lock_guard<std::mutex> guard (lock);

    if (auto it = cache.find (kind); it != cache.end())
        return it->second;

    Layout l;
    switch (kind)
    {
        case EffectKind::fruityLimiter: l = fruityLimiter(); break;
        case EffectKind::fruityCompressor: l = fruityCompressor(); break;
        case EffectKind::fruityFilter: l = fruityFilter(); break;
        case EffectKind::fruityChorus: l = fruityChorus(); break;
        case EffectKind::fruityFlanger: l = fruityFlanger(); break;
        case EffectKind::fruityFastDist: l = fruityFastDist(); break;
        case EffectKind::stereoEnhancer: l = stereoEnhancer(); break;
        case EffectKind::fruityDelay3: l = fruityDelay3(); break;
        case EffectKind::fruityMultiband: l = fruityMultiband(); break;
        case EffectKind::reeverb2: l = reeverb2(); break;
        case EffectKind::luxeVerb: l = luxeVerb(); break;
        case EffectKind::parametricEq: l = parametricEq2(); break;
        case EffectKind::grossBeat: l = grossBeat(); break;
        case EffectKind::fruityDelay2: l = fruityDelay2(); break;
        case EffectKind::reeverb1: l = reeverb1(); break;
        case EffectKind::fruityFlangus: l = fruityFlangus(); break;
        case EffectKind::fruitySoftClipper: l = fruitySoftClipper(); break;
        case EffectKind::fruityBloodOverdrive: l = fruityBloodOverdrive(); break;
        case EffectKind::fruityPanOMatic: l = fruityPanOMatic(); break;
        case EffectKind::stereoShaper: l = stereoShaper(); break;
        case EffectKind::transientProcessor: l = transientProcessor(); break;
        case EffectKind::fruityFreeFilter: l = fruityFreeFilter(); break;
        case EffectKind::fruityWaveShaper: l = fruityWaveShaper(); break;
        case EffectKind::vintageChorus: l = vintageChorus(); break;
        case EffectKind::hyperChorus: l = hyperChorus(); break;
        case EffectKind::fruityBalance: l = fruityBalance(); break;
        case EffectKind::squeeze: l = fruitySqueeze(); break;
        case EffectKind::maximus: l = maximus(); break;
        case EffectKind::vintagePhaser: l = vintagePhaser(); break;
        case EffectKind::ridgeStrip: l = ridgeStrip(); break;
        case EffectKind::glowLeveler: l = glowLeveler(); break;
        case EffectKind::hushDeEsser: l = hushDeEsser(); break;
        case EffectKind::aether: l = aetherLayout(); break;
        case EffectKind::ember: l = emberLayout(); break;
        case EffectKind::lynxTune: l = lynxTune(); break;
        case EffectKind::fruitySend: l = fruitySend(); break;
        default: l = generated (kind); break;
    }
    return cache.emplace (kind, std::move (l)).first->second;
}

const Layout* instrumentLayout (InstrumentKind kind)
{
    static const Layout osc = threeOsc(), keys = flKeys(), boo = booBass(), sytrus = sytrusMain(), harmor = harmorMain();
    switch (kind)
    {
        case InstrumentKind::threeosc: return &osc;
        case InstrumentKind::flkeys: return &keys;
        case InstrumentKind::boobass: return &boo;
        case InstrumentKind::sytrus: return &sytrus;
        case InstrumentKind::harmor: return &harmor;
        default: return nullptr;
    }
}

const std::vector<InstrumentKind>& instrumentLayoutKinds()
{
    static const std::vector<InstrumentKind> kinds { InstrumentKind::threeosc, InstrumentKind::flkeys, InstrumentKind::boobass, InstrumentKind::sytrus, InstrumentKind::harmor };
    return kinds;
}

const Layout& soundgoodizerLayout()
{
    static const Layout l = soundgoodizer();
    return l;
}

bool hasFlLayout (EffectKind kind)
{
    return *layoutFor (kind).fl != 0;
}

bool usesStateParams (EffectKind kind) noexcept
{
    return kind == EffectKind::maximus;
}

int paramCount (EffectKind kind)
{
    return usesStateParams (kind) ? (int) maximusfx::stateParams().size() : (int) effectInfo (kind).params.size();
}

const char* paramName (EffectKind kind, int index)
{
    if (index < 0 || index >= paramCount (kind))
        return "";
    return usesStateParams (kind) ? maximusfx::stateParams()[(std::size_t) index].name : effectInfo (kind).params[(std::size_t) index].name;
}

int paramIndex (EffectKind kind, const char* name)
{
    if (name == nullptr)
        return -1;
    if (usesStateParams (kind))
        return maximusfx::stateParamIndex (name);
    const auto& params = effectInfo (kind).params;
    for (int i = 0; i < (int) params.size(); ++i)
        if (std::strcmp (params[(std::size_t) i].name, name) == 0)
            return i;
    return -1;
}

std::vector<std::string> optionLabels (const LayoutItem& item)
{
    std::vector<std::string> out;
    if (item.options == nullptr)
        return out;
    std::string current;
    for (const char* p = item.options; *p != 0; ++p)
    {
        if (*p == '|')
        {
            out.push_back (current);
            current.clear();
        }
        else
            current += *p;
    }
    out.push_back (current);
    return out;
}

float optionValue (const LayoutItem& item, int option)
{
    if (option >= 0 && option < (int) item.values.size())
        return item.values[(std::size_t) option];
    const auto n = (int) optionLabels (item).size();
    return n > 1 ? (float) option / (float) (n - 1) : 0.0f;
}

int optionAt (const LayoutItem& item, float value)
{
    const auto n = (int) optionLabels (item).size();
    auto best = 0;
    auto distance = 2.0f;
    for (int o = 0; o < n; ++o)
        if (const auto d = std::abs (optionValue (item, o) - value); d < distance)
        {
            distance = d;
            best = o;
        }
    return best;
}

bool isShown (EffectKind kind, const LayoutItem& item, int page, const EffectParams& params)
{
    if (item.page >= 0 && item.page != page)
        return false;
    const auto inRange = [&] (const char* param, float lo, float hi) {
        if (param == nullptr)
            return true;
        const auto i = paramIndex (kind, param);
        const auto v = i >= 0 && i < (int) params.size() ? params[(std::size_t) i] : 0.0f;
        return v >= lo && (v < hi || hi >= 1.0f); // [lo, hi), the top end included at 1
    };
    return inRange (item.whenParam, item.whenMin, item.whenMax) && inRange (item.when2Param, item.when2Min, item.when2Max);
}

bool isValueControl (Item type) noexcept
{
    return type != Item::section && type != Item::label && type != Item::display;
}
} // namespace lynx::plugui
