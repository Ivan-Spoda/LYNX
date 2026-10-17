#include "core/PluginLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

#include "core/Effects.h"

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

LayoutItem toggle (float x, float y, float w, float h, const char* param, const char* caption)
{
    LayoutItem i;
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

LayoutItem display (float x, float y, float w, float h, const char* id, const char* param = nullptr, const char* caption = nullptr)
{
    LayoutItem i;
    i.type = Item::display;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.display = id;
    i.param = param;
    i.label = caption;
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

Layout fruityGrid (const char* fl, int rows)
{
    Layout l;
    l.fl = fl;
    l.width = 501;
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
    v.push_back (section (32, 161, 255, 145, TR_NOOP ("Loudness")));
    v.push_back (section (287, 161, 178, 145, TR_NOOP ("Envelope")));
    v.push_back (section (465, 161, 165, 145, TR_NOOP ("Noise gate")));
    v.push_back (section (117, 180, 345, 120, nullptr, boxed));
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

    // COMP page
    v.push_back (onPage (knob (148, 205, 44, "Comp. threshold", TR_NOOP ("Thres"), 240), 1));
    v.push_back (onPage (knob (202, 205, 44, "Knee", TR_NOOP ("Knee"), 240, bipolar), 1));
    v.push_back (onPage (knob (256, 205, 44, "Ratio", TR_NOOP ("Ratio"), 240, bipolar), 1));
    v.push_back (onPage (knob (316, 205, 44, "Comp. attack", TR_NOOP ("Att"), 240), 1));
    v.push_back (onPage (knob (372, 205, 44, "Comp. release", TR_NOOP ("Rel"), 240), 1));
    v.push_back (onPage (knob (432, 205, 44, "RMS window", TR_NOOP ("RMS"), 240), 1));
    v.push_back (onPage (stepper (358, 259, 28, 30, "Comp. curve", TR_NOOP ("Curve"), 7), 1));
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
    v.push_back (knob (97, 62, 56, "Time", TR_NOOP ("Time"), 130, big));
    v.push_back (toggle (142, 62, 80, 14, "Tempo sync", TR_NOOP ("Tempo sync")));
    v.push_back (toggle (142, 81, 80, 14, "Keep pitch", TR_NOOP ("Keep pitch")));
    v.push_back (slider (Item::hslider, 145, 104, 60, 30, "Time smoothing", TR_NOOP ("Smoothing")));
    v.push_back (knob (233, 82, 34, "Time R / pan", TR_NOOP ("Offset"), 130, bipolar));

    v.push_back (section (257, 2, 121, 110, TR_NOOP ("Delay model")));
    v.push_back (radio (265, 58, 60, 60, "Delay type", TR_NOOP ("Mono|Stereo|Ping pong|Off"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (knob (356, 86, 34, "Stereo spread", TR_NOOP ("Stereo"), 130));

    v.push_back (section (380, 2, 305, 110, TR_NOOP ("Feedback")));
    v.push_back (knob (418, 87, 56, "Feedback", TR_NOOP ("Level"), 130, big));
    v.push_back (knob (469, 86, 34, "Cutoff", TR_NOOP ("Cutoff"), 130));
    v.push_back (knob (516, 86, 34, "Resonance", TR_NOOP ("Res"), 130));
    v.push_back (radio (555, 58, 36, 60, "Filter mode", TR_NOOP ("LP|HP|BP|Off"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (knob (620, 86, 34, "Rate reduce", TR_NOOP ("Smp rate"), 130));
    v.push_back (knob (666, 86, 34, "Bit reduce", TR_NOOP ("Bits"), 130));

    v.push_back (section (688, 2, 62, 216, TR_NOOP ("Output")));
    v.push_back (knob (719, 52, 34, "Wet", TR_NOOP ("Wet"), 80));
    v.push_back (knob (719, 114, 34, "Colour", TR_NOOP ("Tone"), 142, bipolar));
    v.push_back (knob (719, 176, 34, "Dry", TR_NOOP ("Dry"), 204));

    v.push_back (section (62, 114, 193, 104, TR_NOOP ("Modulation")));
    v.push_back (knob (94, 172, 44, "Mod rate", TR_NOOP ("Rate"), 206, big));
    v.push_back (knob (180, 172, 34, "Mod amount", TR_NOOP ("Time"), 206));
    v.push_back (knob (231, 172, 34, "Cutoff mod", TR_NOOP ("Cutoff"), 206));

    v.push_back (section (257, 114, 121, 104, TR_NOOP ("Diffusion")));
    v.push_back (knob (292, 172, 34, "Diffusion", TR_NOOP ("Level"), 206));
    v.push_back (knob (343, 172, 34, "Diffusion spread", TR_NOOP ("Spread"), 206));

    v.push_back (section (380, 114, 305, 104, TR_NOOP ("Feedback distortion")));
    v.push_back (radio (390, 176, 44, 32, "Saturation type", TR_NOOP ("Limit|Sat")));
    v.push_back (knob (476, 172, 34, "Distortion", TR_NOOP ("Level"), 206));
    v.push_back (knob (524, 172, 34, "Hardness", TR_NOOP ("Knee"), 206));
    v.push_back (knob (572, 172, 34, "Symmetry", TR_NOOP ("Symmetry"), 206, bipolar));
    v.push_back (display (614, 140, 68, 72, "shaper", "Hardness"));
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
        v.push_back (knob (x + 19, 58, 32, band.threshold, TR_NOOP ("Thres"), 84));
        v.push_back (knob (x + 65, 58, 32, band.ratio, TR_NOOP ("Ratio"), 84));
        v.push_back (knob (x + 111, 58, 32, band.knee, TR_NOOP ("Knee"), 84));
        v.push_back (knob (x + 19, 120, 32, band.attack, TR_NOOP ("Att"), 144));
        v.push_back (knob (x + 65, 120, 32, band.release, TR_NOOP ("Rel"), 144));
        v.push_back (knob (x + 111, 120, 32, band.gain, TR_NOOP ("Gain"), 144, bipolar));
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
    v.push_back (toggle (256, 194, 18, 12, "Pre-delay sync", nullptr));
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
        v.push_back (ff (toggle (676, 102, 18, 12, "Feed-forward delay 1 sync", nullptr)));
        v.push_back (ff (toggle (676, 194, 18, 12, "Feed-forward delay 2 sync", nullptr)));
    }
    // feedback shifter
    {
        const auto fb = [] (LayoutItem i) { return when (std::move (i), "Pitch type", 0.0f, 0.5f); };
        v.push_back (fb (value (476, 46, 38, 13, "Feedback reverb mix", 0)));
        v.push_back (fb (slider (Item::vslider, 483, 62, 24, 145, "Feedback reverb mix", TR_NOOP ("Reverb mix"))));
        v.push_back (fb (knob (552, 82, 34, "Feedback pitch", TR_NOOP ("Pitch"), 108, valueAbove | bipolar)));
        v.push_back (fb (knob (607, 82, 34, "Feedback gain", TR_NOOP ("Gain"), 108, valueAbove)));
        v.push_back (fb (when (knob (664, 82, 34, "Feedback delay", TR_NOOP ("Delay"), 108, valueAbove), "Feedback delay sync", 0.0f, 0.5f)));
        v.push_back (fb (when (knob (664, 82, 34, "Feedback delay (sync)", TR_NOOP ("Delay"), 108, valueAbove), "Feedback delay sync", 0.5f, 1.0f)));
        v.push_back (fb (toggle (676, 102, 18, 12, "Feedback delay sync", nullptr)));
        v.push_back (fb (knob (552, 174, 34, "Feedback low cut", TR_NOOP ("Low cut"), 200, valueAbove)));
        v.push_back (fb (knob (607, 174, 34, "Feedback high cut", TR_NOOP ("High cut"), 200, valueAbove)));
    }

    v.push_back (section (706, 14, 234, 200, TR_NOOP ("Output")));
    fader (736, "Dry level", TR_NOOP ("Dry"));
    fader (791, "Wet level", TR_NOOP ("Wet"));
    v.push_back (toggle (803, 194, 18, 12, "Limiter", nullptr));
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

// --- generated layouts ------------------------------------------------------------------------------

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
                                                 EffectKind::fruityFlanger, EffectKind::stereoEnhancer };
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
        default: l = generated (kind); break;
    }
    return cache.emplace (kind, std::move (l)).first->second;
}

bool hasFlLayout (EffectKind kind)
{
    return *layoutFor (kind).fl != 0;
}

int paramIndex (EffectKind kind, const char* name)
{
    if (name == nullptr)
        return -1;
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
