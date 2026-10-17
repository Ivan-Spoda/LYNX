// FL arrangements of round 105 (docs/analise/REA_rodada105.md): Fruity Vocoder, Frequency Splitter, Fruity Delay Bank,
// EQUO, Multiband Delay, Fruity Convolver, Pitch Shifter, Hardcore, Effector, Vocodex. Included by PluginLayout.cpp
// inside its anonymous namespace (it uses the builders defined there). Positions measured on FL Studio 2026's windows
// (captures in tmp/r93/flref and tmp/r105/flref, never in the repository), one unit = one FL pixel, body origin.

LayoutItem menu (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {})
{
    LayoutItem i;
    i.type = Item::menu;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.options = options;
    i.values = std::move (values);
    return i;
}

// a read-out on a plate above a fader or knob (FL's green value boxes)
LayoutItem plate (float x, float y, float w, float h, const char* param)
{
    return value (x, y, w, h, param, boxed);
}

// a page selector that sets one digit of the editor's page: page = … + option · stride (two selectors share a page:
// EQUO's bank and target, Multiband Delay's)
LayoutItem pageDigit (float x, float y, float w, float h, const char* options, int stride)
{
    auto i = pageTabs (x, y, w, h, options);
    i.steps = stride;
    return i;
}

// a knob without caption plus its caption above it (FL's Delay Bank globals)
void knobCaptionAbove (std::vector<LayoutItem>& v, float cx, float cy, float d, const char* param, const char* caption, float captionY)
{
    v.push_back (knob (cx, cy, d, param, nullptr, 0, noCaption));
    v.push_back (label (cx - 24, captionY - 6, 48, 12, caption));
}

// the name of a state control (core/StateParams.h) as a stable C string for a layout item
const char* stateName (EffectKind kind, const std::string& name)
{
    const auto* table = stateparams::tableFor (kind);
    const auto i = table != nullptr ? table->index (name.c_str()) : -1;
    return i >= 0 ? table->name (i) : "";
}

Layout fruityVocoder()
{
    // FREQ (FORM fader, MIN, MAX, SCALE, BW, INVERT) · ENV (ATT, REL) · MIX (listen modulator / carrier / mix LEDs,
    // the L / R inputs, MOD, CAR, WET faders) over the band display with HOLD, BANDS 4…128 and FILTER 1…3
    Layout l;
    l.fl = "Fruity Vocoder";
    l.width = 416;
    l.height = 306;
    auto& v = l.items;
    v.push_back (section (12, 6, 201, 143, TR_NOOP ("Freq")));
    v.push_back (slider (Item::vslider, 22, 30, 20, 100, "Formant", nullptr, noCaption | bipolar));
    v.push_back (label (8, 132, 48, 12, TR_NOOP ("Form")));
    v.push_back (knob (83, 50, 32, "Min freq.", TR_NOOP ("Min"), 79));
    v.push_back (knob (132, 50, 32, "Max freq.", TR_NOOP ("Max"), 79));
    v.push_back (knob (181, 50, 32, "Scale", TR_NOOP ("Scale"), 79, bipolar));
    v.push_back (knob (83, 109, 32, "Width", TR_NOOP ("BW"), 138));
    v.push_back (toggle (172, 97, 20, 12, "Invert", nullptr));
    v.push_back (label (157, 113, 48, 12, TR_NOOP ("Invert")));
    v.push_back (section (219, 6, 62, 143, TR_NOOP ("Env")));
    v.push_back (knob (250, 50, 32, "Attack", TR_NOOP ("Att"), 79));
    v.push_back (knob (250, 109, 32, "Release", TR_NOOP ("Rel"), 138));
    v.push_back (section (287, 6, 116, 143, TR_NOOP ("Mix")));
    v.push_back (toggle (306, 18, 16, 13, "Swap L/R", "L", boxed));
    v.push_back (toggle (338, 18, 16, 13, "Swap L/R", "R", boxed));
    v.push_back (tabs (298, 34, 96, 9, "Listen", TR_NOOP ("Modulator|Carrier|Mix")));
    v.push_back (slider (Item::vslider, 303, 48, 20, 82, "Modulator", nullptr, noCaption));
    v.push_back (slider (Item::vslider, 335, 48, 20, 82, "Carrier", nullptr, noCaption));
    v.push_back (slider (Item::vslider, 368, 48, 20, 82, "Wet", nullptr, noCaption));
    v.push_back (label (295, 133, 36, 12, TR_NOOP ("Mod")));
    v.push_back (label (327, 133, 36, 12, TR_NOOP ("Car")));
    v.push_back (label (360, 133, 36, 12, TR_NOOP ("Wet")));
    v.push_back (section (13, 160, 388, 132, nullptr, boxed | raised));
    v.push_back (label (18, 163, 200, 18, "Fruity Vocoder", dim | alignLeft));
    v.push_back (toggle (345, 165, 50, 15, "Hold", TR_NOOP ("Hold"), boxed));
    v.push_back (display (18, 183, 378, 86, "vocbands", "Bands"));
    v.push_back (label (17, 273, 54, 14, TR_NOOP ("Bands"), alignLeft));
    v.push_back (tabs (76, 273, 154, 14, "Bands", "4|8|16|32|64|128"));
    v.push_back (label (285, 273, 54, 14, TR_NOOP ("Filter")));
    v.push_back (tabs (343, 273, 53, 14, "Order", "1|2|3"));
    l.menuSwitches = { "Side inputs" };
    return l;
}

Layout frequencySplitterLayout()
{
    // the band graph on top; CROSSOVER (slope, freq, bands 2 / 3, freq, slope) · MAIN OUT (low / mid / high level and
    // their switches) · SENDS (each band's send: output switch, level and switch); linear phase, precision and speed at
    // the bottom. The solo switches (FL: behind the panels' arrow buttons, not confirmed) are in the options menu.
    Layout l;
    l.fl = "Frequency Splitter";
    l.width = 776;
    l.height = 340;
    auto& v = l.items;
    v.push_back (display (10, 6, 755, 214, "fsplit"));
    v.push_back (section (10, 223, 282, 80, nullptr, boxed));
    v.push_back (stepper (19, 243, 48, 19, "Low/mid slope", nullptr, 7));
    v.push_back (label (19, 267, 48, 12, TR_NOOP ("Slope")));
    v.push_back (knob (97, 250, 40, "Low/mid freq.", TR_NOOP ("Freq"), 285));
    v.push_back (toggle (136, 245, 30, 13, "3 bands", "3", boxed));
    v.push_back (label (127, 263, 48, 12, TR_NOOP ("Bands")));
    v.push_back (knob (202, 250, 40, "Mid/high freq.", TR_NOOP ("Freq"), 285));
    v.push_back (stepper (237, 243, 48, 19, "Mid/high slope", nullptr, 7));
    v.push_back (label (237, 267, 48, 12, TR_NOOP ("Slope")));
    v.push_back (label (14, 289, 100, 12, TR_NOOP ("Crossover"), dim | alignLeft));
    v.push_back (section (295, 223, 190, 80, nullptr, boxed));
    const char* const outs[3] { "Low level", "Mid level", "High level" };
    const char* const outMutes[3] { "Low mute", "Mid mute", "High mute" };
    const char* const sends[3] { "Low send", "Mid send", "High send" };
    const char* const sendMutes[3] { "Low send mute", "Mid send mute", "High send mute" };
    const char* const sendOut[3] { "Out: low", "Out: mid", "Out: high" };
    const char* const names[3] { TR_NOOP ("Low"), TR_NOOP ("Mid"), TR_NOOP ("High") };
    for (int b = 0; b < 3; ++b)
    {
        const auto cx = 337.0f + 59.0f * (float) b;
        auto k = knob (cx, 248, 32, outs[b], nullptr, 0, noCaption);
        k.colour = b;
        v.push_back (k);
        v.push_back (toggle (cx - 25, 268, 20, 11, outMutes[b], nullptr));
        v.push_back (label (cx - 6, 268, 30, 11, names[b], alignLeft));
    }
    v.push_back (label (299, 289, 100, 12, TR_NOOP ("Main out"), dim | alignLeft));
    v.push_back (section (490, 223, 275, 80, nullptr, boxed));
    for (int b = 0; b < 3; ++b)
    {
        const auto x0 = 497.0f + 90.0f * (float) b;
        v.push_back (toggle (x0, 241, 38, 18, sendOut[b], TR_NOOP ("Out"), boxed));
        auto k = knob (x0 + 65.0f, 248, 32, sends[b], nullptr, 0, noCaption);
        k.colour = b;
        v.push_back (k);
        v.push_back (toggle (x0 + 40.0f, 268, 20, 11, sendMutes[b], nullptr));
        v.push_back (label (x0 + 59.0f, 268, 30, 11, names[b], alignLeft));
    }
    v.push_back (label (494, 289, 100, 12, TR_NOOP ("Sends"), dim | alignLeft));
    v.push_back (toggle (12, 313, 100, 16, "FFT mode", TR_NOOP ("Linear phase")));
    v.push_back (label (114, 313, 46, 16, TR_NOOP ("Precision")));
    v.push_back (menu (162, 313, 62, 16, "FFT quality", TR_NOOP ("Lowest|Low|Medium|High|Higher|Highest"), { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }));
    v.push_back (label (229, 313, 40, 16, TR_NOOP ("Speed")));
    v.push_back (slider (Item::hslider, 272, 313, 70, 16, "FFT fade", nullptr, noCaption));
    v.push_back (label (560, 306, 200, 28, "Frequency Splitter", dim));
    l.menuSwitches = { "Low solo", "Mid solo", "High solo", "Low send solo", "Mid send solo", "High send solo" };
    return l;
}

Layout fruityDelayBankLayout()
{
    // bank switches (LEDs) and the bank tabs 1…8 on top, OVERSAMPLE, DRY / WET / IN / FB; then the selected bank:
    // IN (pan, vol) · FILTER (type, sections, post, gain, cut, res) · FEEDBACK (pattern tap, tempo sync, mode
    // off / inv / norm / ping-pong, time / offset / separation faders, pan, vol) · FB FILTER · GRAIN (div, shape) ·
    // OUT (send to the next bank, pan, vol). Everything but the four globals lives in the slot state.
    constexpr auto k = EffectKind::fruityDelayBank;
    Layout l;
    l.fl = "Fruity Delay Bank";
    l.width = 583;
    l.height = 278;
    l.pages = { "1", "2", "3", "4", "5", "6", "7", "8" };
    auto& v = l.items;
    v.push_back (section (6, 4, 571, 58, nullptr, boxed));
    for (int b = 0; b < 8; ++b)
        v.push_back (toggle (17.0f + 28.0f * (float) b, 16, 13, 10, stateName (k, stateparams::bankControl (b, "On")), nullptr, boxed));
    v.push_back (pageTabs (10, 33, 222, 26, "1|2|3|4|5|6|7|8"));
    LayoutItem ovs = menu (322, 31, 70, 16, stateName (k, "Oversample"), TR_NOOP ("Off|2x|4x"), { 0.0f, 0.5f, 1.0f });
    v.push_back (ovs);
    v.push_back (label (322, 47, 70, 11, TR_NOOP ("Oversample")));
    knobCaptionAbove (v, 418, 41, 28, "Dry", TR_NOOP ("Dry"), 13);
    knobCaptionAbove (v, 463, 41, 28, "Output", TR_NOOP ("Wet"), 13);
    knobCaptionAbove (v, 507, 41, 28, "Input", TR_NOOP ("In"), 13);
    knobCaptionAbove (v, 552, 41, 28, "Feedback", TR_NOOP ("FB"), 13);

    const auto filterTypes = TR_NOOP ("Off|Low pass|Band pass|Notch|High pass|Low shelf|Peaking|High shelf");
    const std::vector<float> eighths { 0.0f, 1.0f / 7, 2.0f / 7, 3.0f / 7, 4.0f / 7, 5.0f / 7, 6.0f / 7, 1.0f };
    for (int b = 0; b < 8; ++b)
    {
        const auto p = [&] (const char* control) { return stateName (k, stateparams::bankControl (b, control)); };
        const auto add = [&] (LayoutItem i) { v.push_back (onPage (std::move (i), b)); };
        const auto dial = [&] (float cx, float cy, const char* control, const char* caption, std::uint16_t style = 0) {
            add (knob (cx, cy, 30, p (control), caption, cy + 29, style));
        };
        const auto fader = [&] (float cx, const char* control, const char* caption, std::uint16_t style = 0) {
            add (slider (Item::vslider, cx - 10, 140, 20, 108, p (control), nullptr, noCaption | style));
            add (label (cx - 18, 255, 36, 12, caption));
        };
        add (section (10, 68, 56, 205, TR_NOOP ("In")));
        dial (37, 175, "Input pan", TR_NOOP ("Pan"), bipolar);
        dial (37, 233, "Input volume", TR_NOOP ("Vol"), bipolar);

        add (section (70, 68, 107, 205, TR_NOOP ("Filter")));
        add (menu (76, 98, 46, 16, p ("Filter 1 type"), filterTypes, eighths));
        add (tabs (125, 98, 45, 16, p ("Filter 1 order"), "1|2|3"));
        add (toggle (90, 172, 16, 10, p ("Filter 1 post"), nullptr, boxed));
        add (label (74, 199, 46, 12, TR_NOOP ("Post")));
        dial (145, 175, "Filter 1 gain", TR_NOOP ("Gain"), bipolar);
        dial (97, 233, "Filter 1 cutoff", TR_NOOP ("Cut"));
        dial (149, 233, "Filter 1 resonance", TR_NOOP ("Res"));

        add (section (181, 68, 149, 205, TR_NOOP ("Feedback")));
        add (toggle (188, 88, 14, 13, p ("Tap"), "|", boxed));
        add (toggle (188, 107, 14, 13, p ("Sync"), "T", boxed));
        LayoutItem mode = radio (210, 88, 116, 32, p ("Mode"), TR_NOOP ("Off|Norm|Inv|P.pong"), { 0.0f, 1.0f / 3, 2.0f / 3, 1.0f });
        mode.columns = 2;
        add (mode);
        fader (194, "Time", TR_NOOP ("Time"));
        fader (228, "Stereo offset", TR_NOOP ("Ofs"), bipolar);
        fader (263, "Feedback cross", TR_NOOP ("Sep"), bipolar);
        dial (303, 175, "Feedback pan", TR_NOOP ("Pan"), bipolar);
        dial (303, 233, "Feedback level", TR_NOOP ("Vol"), bipolar);

        add (section (335, 68, 107, 205, TR_NOOP ("FB filter")));
        add (menu (341, 98, 46, 16, p ("Filter 2 type"), filterTypes, eighths));
        add (tabs (390, 98, 45, 16, p ("Filter 2 order"), "1|2|3"));
        dial (408, 175, "Filter 2 gain", TR_NOOP ("Gain"), bipolar);
        dial (362, 233, "Filter 2 cutoff", TR_NOOP ("Cut"));
        dial (413, 233, "Filter 2 resonance", TR_NOOP ("Res"));

        add (section (446, 68, 65, 205, TR_NOOP ("Grain")));
        fader (462, "Steps", TR_NOOP ("Div"));
        fader (495, "Shape", TR_NOOP ("Sh"), bipolar);

        add (section (516, 68, 56, 205, TR_NOOP ("Out")));
        if (b < 7)
            dial (543, 116, "Send to next", TR_NOOP ("Next"), bipolar);
        dial (543, 175, "Output pan", TR_NOOP ("Pan"), bipolar);
        dial (543, 233, "Output volume", TR_NOOP ("Vol"), bipolar);
    }
    // the solo bank (FL: right-click on a bank tab, not confirmed) and bank 8's missing send
    l.editedElsewhere = { stateName (k, "Solo"), stateName (k, stateparams::bankControl (7, "Unused")) };
    return l;
}

Layout equoLayout()
{
    // GRAPHIC EQ: the 31 bands of the selected bank and target (drag to draw), BANK M / 1…8, VOL / PAN / SEND;
    // MORPH (shift, mix, morph, smooth, vol) and SETUP (send only, add mode, oversample, bandwidth). The page is the
    // target (×1) and the bank (×3).
    Layout l;
    l.fl = "EQUO";
    l.width = 572;
    l.height = 245;
    auto& v = l.items;
    v.push_back (section (12, 10, 388, 220, nullptr, boxed | raised));
    v.push_back (label (19, 13, 150, 22, "Graphic EQ", dim | alignLeft));
    v.push_back (display (19, 40, 371, 160, "equobars"));
    v.push_back (label (19, 208, 36, 15, TR_NOOP ("Bank"), boxed));
    v.push_back (pageDigit (58, 208, 164, 15, "M|1|2|3|4|5|6|7|8", 3));
    v.push_back (pageDigit (267, 208, 123, 15, TR_NOOP ("Vol|Pan|Send"), 1));
    v.push_back (section (407, 5, 155, 130, TR_NOOP ("Morph")));
    v.push_back (knob (426, 45, 30, "Band shift", TR_NOOP ("Shift"), 71, bipolar));
    v.push_back (knob (483, 45, 30, "Band mix", TR_NOOP ("Mix"), 71, bipolar));
    v.push_back (knob (542, 45, 30, "Morph", TR_NOOP ("Morph"), 71));
    v.push_back (knob (457, 100, 30, "Smoothness", TR_NOOP ("Smooth"), 126));
    v.push_back (knob (515, 100, 30, "Volume", TR_NOOP ("Vol"), 126));
    v.push_back (section (407, 138, 155, 105, TR_NOOP ("Setup")));
    v.push_back (toggle (410, 160, 100, 12, "Send only", TR_NOOP ("Send only")));
    v.push_back (toggle (410, 205, 100, 12, "Add mode", TR_NOOP ("Add mode")));
    v.push_back (toggle (410, 224, 100, 12, "Oversample", TR_NOOP ("Oversample")));
    v.push_back (knob (539, 200, 28, "Bandwidth", TR_NOOP ("BW"), 229));
    // the band cells are drawn in the graph (core/StateParams.h: "Bank 1 volume 1" …)
    for (const auto& p : stateparams::tableFor (EffectKind::equo)->params)
        l.editedElsewhere.push_back (p.name.c_str());
    return l;
}

Layout multibandDelayLayout()
{
    // the 16 bands of the selected bank and target (drag to draw) with their frequencies; DELAY / VOL / PAN, BANK 1…8;
    // crossover mode · SCALE (×0.1, harmonic, weighted), FEEDBACK · SMOOTHING TIME, KEEP PITCH · MORPH · WET (comp / sat /
    // clip), HP · MIX. The selected target and bank are FL's (saved in the state).
    constexpr auto k = EffectKind::multibandDelay;
    Layout l;
    l.fl = "Multiband Delay";
    l.width = 568;
    l.height = 292;
    auto& v = l.items;
    v.push_back (display (11, 8, 544, 180, "mbdbars"));
    v.push_back (tabs (93, 195, 143, 15, stateName (k, "Target"), TR_NOOP ("Delay|Vol|Pan")));
    v.push_back (label (245, 195, 35, 15, TR_NOOP ("Bank")));
    v.push_back (tabs (282, 195, 134, 15, stateName (k, "Bank"), "1|2|3|4|5|6|7|8"));
    v.push_back (label (432, 192, 123, 20, "Multiband Delay", dim));
    v.push_back (radio (11, 226, 80, 44, "Filter mode", TR_NOOP ("Gentle|Steep|Linear phase")));
    v.push_back (section (92, 222, 143, 50, nullptr, boxed | raised));
    v.push_back (knob (118, 247, 32, "Delay scale", nullptr, 0, noCaption | bipolar));
    v.push_back (toggle (145, 230, 40, 11, "Delay x0.1", TR_NOOP ("x 0.1")));
    v.push_back (toggle (145, 254, 16, 14, "Harmonic scale", "H", boxed));
    v.push_back (toggle (167, 254, 16, 14, "Weighting", "W", boxed));
    v.push_back (knob (209, 247, 32, "Feedback", nullptr, 0, noCaption));
    v.push_back (label (94, 275, 48, 12, TR_NOOP ("Scale")));
    v.push_back (label (182, 275, 54, 12, TR_NOOP ("Feedback")));
    v.push_back (section (241, 222, 116, 50, nullptr, boxed | raised));
    v.push_back (knob (267, 247, 32, "Smoothing", nullptr, 0, noCaption));
    v.push_back (toggle (290, 230, 66, 11, "Keep pitch", TR_NOOP ("Keep pitch")));
    v.push_back (label (241, 275, 116, 12, TR_NOOP ("Smoothing time")));
    v.push_back (section (363, 222, 50, 50, nullptr, boxed | raised));
    v.push_back (knob (388, 247, 32, "Morph", nullptr, 0, noCaption));
    v.push_back (label (363, 275, 50, 12, TR_NOOP ("Morph")));
    v.push_back (section (420, 222, 90, 50, nullptr, boxed | raised));
    v.push_back (radio (424, 226, 42, 44, "Saturation", TR_NOOP ("Comp|Sat|Clip")));
    v.push_back (knob (488, 247, 28, "High pass", nullptr, 0, noCaption));
    v.push_back (label (420, 275, 46, 12, TR_NOOP ("Wet")));
    v.push_back (label (470, 275, 36, 12, TR_NOOP ("HP")));
    v.push_back (knob (536, 247, 34, "Mix", nullptr, 0, noCaption | bipolar));
    v.push_back (label (514, 275, 44, 12, TR_NOOP ("Mix")));
    for (int i = 0; i < mbdelay::numBanks * 48; ++i)
        l.editedElsewhere.push_back (stateparams::tableFor (k)->name (i));
    return l;
}

Layout convolverLayout()
{
    // dry, in (input separation), wet, wet separation · delay, self-conv, stretch, eq (the state's knobs); NORMALIZE;
    // the impulse response's waveform with its rate, format and length. FL's sample editor tools and its equalizer page
    // are not reproduced (LYNX keeps the state's EQ curve as it is).
    constexpr auto k = EffectKind::convolver;
    Layout l;
    l.fl = "Fruity Convolver";
    l.width = 722;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (2, 2, 718, 96, nullptr, boxed));
    v.push_back (knob (41, 32, 40, "Dry", TR_NOOP ("Dry"), 68));
    v.push_back (knob (112, 36, 28, "Input separation", TR_NOOP ("In"), 68, bipolar));
    v.push_back (knob (162, 32, 40, "Wet", TR_NOOP ("Wet"), 68));
    v.push_back (knob (211, 36, 28, "Wet separation", TR_NOOP ("Wet"), 68, bipolar));
    v.push_back (knob (281, 32, 40, stateName (k, "Predelay"), TR_NOOP ("Delay"), 68));
    v.push_back (knob (340, 32, 40, stateName (k, "Self-conv"), TR_NOOP ("Self-conv"), 68));
    v.push_back (knob (397, 32, 40, stateName (k, "Stretch"), TR_NOOP ("Stretch"), 68, bipolar));
    v.push_back (knob (456, 32, 40, stateName (k, "EQ"), TR_NOOP ("EQ"), 68));
    v.push_back (toggle (622, 8, 90, 12, stateName (k, "Normalize"), TR_NOOP ("Normalize")));
    v.push_back (label (560, 28, 150, 40, "Fruity Convolver", dim));
    v.push_back (display (5, 104, 712, 20, "irinfo"));
    v.push_back (display (5, 128, 712, 168, "irwave"));
    return l;
}
