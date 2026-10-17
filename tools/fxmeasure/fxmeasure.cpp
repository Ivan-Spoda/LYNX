// fxmeasure — black-box measurement host for third-party audio effects (VST3), Windows console tool.
//
// It hosts a plugin the way a DAW does — public parameter API only (names, ranges, value <-> text) — feeds test
// signals from WAV files and records the outputs. No editor is opened and nothing inside the plugin is inspected
// (round 99 rule: black-box only). Build: tools/fxmeasure/build.sh. Analysis scripts: tools/fxmeasure/py.
//
//   fxmeasure.exe script.txt          (one command per line; '#' starts a comment)
//
// Commands:
//   load <file.vst3> [rate] [block]   load the first effect in the file (default 48000 Hz, 512 samples)
//   rate <hz> [block]                 re-prepare at another sample rate / block size
//   sidechain on|off                  enable every input bus (main + sidechain) or only the main one
//   nonrealtime 0|1                   AudioProcessor::setNonRealtime
//   params <out.tsv> [steps]          parameter table: index, id, name, label, default, steps, texts at k/steps
//   texts <name|#index> <out.tsv> [steps]   value -> text for one parameter at a finer grid
//   set <name|#index> <normalized>    set a parameter (normalized 0..1)
//   settext <name|#index> <text>      set a parameter from its text (plugin's own text -> value)
//   get <name|#index>                 print value and text
//   defaults                          every parameter back to its default value
//   reset                             AudioProcessor::reset
//   settle <seconds>                  process silence (parameter smoothing, envelopes)
//   process <in.wav> <out.wav> [sc.wav]   run the input (and optional sidechain) through, write a 32-bit float WAV
//   latency                           print the reported latency in samples
//   savestate <file> / loadstate <file>
//   pump <ms>                         deliver the plugin's pending Win32 messages for a while
//   echo <text>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

namespace
{
struct Host
{
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    double rate = 48000.0;
    int block = 512;
    bool sidechain = false;
    juce::MidiBuffer midi;

    struct PlayHead final : juce::AudioPlayHead
    {
        juce::int64 samples = 0;
        double rate = 48000.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (120.0);
            p.setTimeSignature (TimeSignature { 4, 4 });
            p.setTimeInSamples (samples);
            p.setTimeInSeconds ((double) samples / rate);
            p.setPpqPosition ((double) samples / rate * 2.0);
            p.setIsPlaying (true);
            return p;
        }
    } playHead;

    void log (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }

    bool prepare()
    {
        if (plugin == nullptr) return false;
        plugin->releaseResources();
        auto layout = plugin->getBusesLayout();
        for (int b = 1; b < layout.inputBuses.size(); ++b)
            if (sidechain) layout.inputBuses.getReference (b) = juce::AudioChannelSet::stereo();
            else layout.inputBuses.getReference (b) = juce::AudioChannelSet::disabled();
        if (! plugin->setBusesLayout (layout))
        {
            // fall back to enabling the buses one by one
            for (int b = 1; b < plugin->getBusCount (true); ++b)
                if (auto* bus = plugin->getBus (true, b)) bus->enable (sidechain);
        }
        plugin->setRateAndBufferSizeDetails (rate, block);
        plugin->setPlayHead (&playHead);
        playHead.rate = rate;
        plugin->prepareToPlay (rate, block);
        log ("prepared rate=" + juce::String (rate) + " block=" + juce::String (block) + " in=" + juce::String (plugin->getTotalNumInputChannels())
             + " out=" + juce::String (plugin->getTotalNumOutputChannels()) + " mainIn=" + juce::String (plugin->getMainBusNumInputChannels())
             + " latency=" + juce::String (plugin->getLatencySamples()));
        return true;
    }

    juce::AudioProcessorParameter* find (const juce::String& key)
    {
        if (plugin == nullptr) return nullptr;
        const auto& ps = plugin->getParameters();
        if (key.startsWithChar ('#'))
        {
            const int i = key.substring (1).getIntValue();
            return juce::isPositiveAndBelow (i, ps.size()) ? ps[i] : nullptr;
        }
        for (auto* p : ps)
            if (p->getName (256) == key) return p;
        for (auto* p : ps)
            if (p->getName (256).equalsIgnoreCase (key)) return p;
        return nullptr;
    }

    // the plugin's asynchronous messages (latency changes, parameter echoes) are delivered through the Win32 queue
    static void pump (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        do
        {
            MSG msg;
            while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage (&msg);
                DispatchMessageW (&msg);
            }
            if (ms > 0) Sleep (1);
        } while (juce::Time::getMillisecondCounter() < end);
    }

    void runSilence (int samples)
    {
        pump (0);
        const int ch = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (ch, block);
        while (samples > 0)
        {
            const int n = juce::jmin (block, samples);
            buf.setSize (ch, n, false, false, true);
            buf.clear();
            midi.clear();
            plugin->processBlock (buf, midi);
            playHead.samples += n;
            samples -= n;
        }
    }

    bool readWav (const juce::File& f, juce::AudioBuffer<float>& out)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
        if (r == nullptr) return false;
        out.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
        return true;
    }

    void process (const juce::File& in, const juce::File& outFile, const juce::File& sc)
    {
        juce::AudioBuffer<float> input, side;
        if (! readWav (in, input)) { log ("error: cannot read " + in.getFullPathName()); return; }
        const bool haveSide = sc != juce::File() && readWav (sc, side);
        const int mainIn = juce::jmax (1, plugin->getMainBusNumInputChannels());
        const int totalIn = plugin->getTotalNumInputChannels();
        const int totalOut = plugin->getTotalNumOutputChannels();
        const int ch = juce::jmax (totalIn, totalOut);
        const int len = input.getNumSamples();
        juce::AudioBuffer<float> result (totalOut, len);
        juce::AudioBuffer<float> buf (ch, block);
        for (int pos = 0; pos < len; pos += block)
        {
            const int n = juce::jmin (block, len - pos);
            buf.setSize (ch, n, false, false, true);
            buf.clear();
            for (int c = 0; c < juce::jmin (mainIn, totalIn); ++c)
                buf.copyFrom (c, 0, input, juce::jmin (c, input.getNumChannels() - 1), pos, n);
            if (haveSide)
                for (int c = mainIn; c < totalIn; ++c)
                    buf.copyFrom (c, 0, side, juce::jmin (c - mainIn, side.getNumChannels() - 1), juce::jmin (pos, side.getNumSamples() - n), n);
            midi.clear();
            plugin->processBlock (buf, midi);
            playHead.samples += n;
            for (int c = 0; c < totalOut; ++c)
                result.copyFrom (c, pos, buf, c, 0, n);
        }
        outFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (outFile.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned) totalOut, 32, {}, 0));
        if (w == nullptr) { log ("error: cannot write " + outFile.getFullPathName()); return; }
        os.release();
        w->writeFromAudioSampleBuffer (result, 0, len);
        w.reset();
        log ("processed " + juce::String (len) + " samples -> " + outFile.getFileName() + " latency=" + juce::String (plugin->getLatencySamples()));
    }

    static juce::String norm (float v) { return juce::String (v, 6); }

    void params (const juce::File& out, int steps)
    {
        juce::String t;
        t << "index\tid\tname\tlabel\tdefault\tdefaultText\tnumSteps\tdiscrete\tboolean\tautomatable";
        for (int k = 0; k <= steps; ++k) t << "\tv" << norm ((float) k / (float) steps);
        t << "\n";
        const auto& ps = plugin->getParameters();
        for (int i = 0; i < ps.size(); ++i)
        {
            auto* p = ps[i];
            juce::String id;
            if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (p)) id = hp->getParameterID();
            const float d = p->getDefaultValue();
            t << i << "\t" << id << "\t" << p->getName (256) << "\t" << p->getLabel() << "\t" << norm (d) << "\t" << p->getText (d, 256) << "\t"
              << p->getNumSteps() << "\t" << (int) p->isDiscrete() << "\t" << (int) p->isBoolean() << "\t" << (int) p->isAutomatable();
            for (int k = 0; k <= steps; ++k) t << "\t" << p->getText ((float) k / (float) steps, 256);
            t << "\n";
        }
        out.replaceWithText (t);
        log ("params " + juce::String (ps.size()) + " -> " + out.getFileName());
    }

    void texts (juce::AudioProcessorParameter* p, const juce::File& out, int steps)
    {
        juce::String t;
        for (int k = 0; k <= steps; ++k)
        {
            const float v = (float) k / (float) steps;
            t << norm (v) << "\t" << p->getText (v, 256) << "\n";
        }
        out.replaceWithText (t);
        log ("texts " + p->getName (256) + " -> " + out.getFileName());
    }

    // text -> normalized value: numbers go through the plugin's own parser; names (enumerations) are found by scanning
    // the value -> text mapping (the middle of the range that shows the text)
    static float textToValue (juce::AudioProcessorParameter* p, const juce::String& text)
    {
        const auto first = text.trimStart()[0];
        const int n = 4000;
        if ((first >= '0' && first <= '9') || first == '-' || first == '+' || first == '.')
        {
            const double want = text.getDoubleValue();
            const float v = p->getValueForText (text);
            const double got = p->getText (v, 256).getDoubleValue();
            if (std::abs (got - want) <= 1e-3 * juce::jmax (1.0, std::abs (want)))
                return v;
            int lo = -1, hi = -1; // stepped numbers the parser does not take: scan for the value
            for (int k = 0; k <= n; ++k)
                if (std::abs (p->getText ((float) k / n, 256).getDoubleValue() - want) <= 1e-6 * juce::jmax (1.0, std::abs (want))) { if (lo < 0) lo = k; hi = k; }
                else if (lo >= 0) break;
            return lo < 0 ? v : (float) (lo + hi) / (2.0f * n);
        }
        int lo = -1, hi = -1;
        for (int k = 0; k <= n; ++k)
            if (p->getText ((float) k / n, 256).trim().equalsIgnoreCase (text.trim())) { if (lo < 0) lo = k; hi = k; }
            else if (lo >= 0) break;
        return lo < 0 ? p->getValueForText (text) : (float) (lo + hi) / (2.0f * n);
    }

    void setParam (juce::AudioProcessorParameter* p, float v)
    {
        p->setValue (v);
    }

    void run (const juce::StringArray& lines)
    {
        for (auto line : lines)
        {
            line = line.upToFirstOccurrenceOf ("#", false, false).trim();
            if (line.isEmpty()) continue;
            juce::StringArray a;
            a.addTokens (line, " \t", "\"");
            a.removeEmptyStrings();
            for (auto& s : a) s = s.unquoted();
            const auto cmd = a[0];

            pump (0);
            if (cmd == "pump") { pump (a[1].getIntValue()); continue; }
            if (cmd == "echo") { log (line.fromFirstOccurrenceOf (" ", false, false)); continue; }
            if (cmd == "load")
            {
                if (a.size() > 2) rate = a[2].getDoubleValue();
                if (a.size() > 3) block = a[3].getIntValue();
                juce::OwnedArray<juce::PluginDescription> types;
                for (auto* f : formats.getFormats())
                    if (f->fileMightContainThisPluginType (a[1])) f->findAllTypesForFile (types, a[1]);
                if (types.isEmpty()) { log ("error: no plugin in " + a[1]); continue; }
                juce::String err;
                plugin = formats.createPluginInstance (*types[0], rate, block, err);
                if (plugin == nullptr) { log ("error: " + err); continue; }
                log ("loaded " + types[0]->name + " by " + types[0]->manufacturerName + " v" + types[0]->version + " params=" + juce::String (plugin->getParameters().size())
                     + " inBuses=" + juce::String (plugin->getBusCount (true)) + " outBuses=" + juce::String (plugin->getBusCount (false)));
                prepare();
                continue;
            }
            if (plugin == nullptr) { log ("error: no plugin loaded for " + cmd); continue; }
            if (cmd == "rate") { rate = a[1].getDoubleValue(); if (a.size() > 2) block = a[2].getIntValue(); prepare(); }
            else if (cmd == "sidechain") { sidechain = a[1] == "on"; prepare(); }
            else if (cmd == "nonrealtime") { plugin->setNonRealtime (a[1].getIntValue() != 0); log ("nonrealtime " + a[1]); }
            else if (cmd == "params") params (juce::File (a[1]), a.size() > 2 ? a[2].getIntValue() : 10);
            else if (cmd == "texts")
            {
                if (auto* p = find (a[1])) texts (p, juce::File (a[2]), a.size() > 3 ? a[3].getIntValue() : 200);
                else log ("error: no parameter " + a[1]);
            }
            else if (cmd == "set" || cmd == "settext")
            {
                auto* p = find (a[1]);
                if (p == nullptr) { log ("error: no parameter " + a[1]); continue; }
                const auto valueText = line.fromFirstOccurrenceOf (a[1], false, false).trim().unquoted();
                const float v = cmd == "set" ? valueText.getFloatValue() : textToValue (p, valueText);
                setParam (p, juce::jlimit (0.0f, 1.0f, v));
                log ("set " + p->getName (256) + " = " + norm (p->getValue()) + " (" + p->getCurrentValueAsText() + ")");
            }
            else if (cmd == "get")
            {
                if (auto* p = find (a[1])) log ("get " + p->getName (256) + " = " + norm (p->getValue()) + " (" + p->getCurrentValueAsText() + ")");
                else log ("error: no parameter " + a[1]);
            }
            else if (cmd == "defaults")
            {
                for (auto* p : plugin->getParameters()) setParam (p, p->getDefaultValue());
                log ("defaults");
            }
            else if (cmd == "reset") { plugin->reset(); log ("reset"); }
            else if (cmd == "settle") runSilence ((int) (a[1].getDoubleValue() * rate));
            else if (cmd == "process") process (juce::File (a[1]), juce::File (a[2]), a.size() > 3 ? juce::File (a[3]) : juce::File());
            else if (cmd == "latency") { pump (50); log ("latency " + juce::String (plugin->getLatencySamples())); }
            else if (cmd == "savestate")
            {
                juce::MemoryBlock m;
                plugin->getStateInformation (m);
                juce::File (a[1]).replaceWithData (m.getData(), m.getSize());
                log ("savestate " + juce::String ((int) m.getSize()) + " bytes");
            }
            else if (cmd == "loadstate")
            {
                juce::MemoryBlock m;
                juce::File (a[1]).loadFileAsData (m);
                plugin->setStateInformation (m.getData(), (int) m.getSize());
                log ("loadstate");
            }
            else log ("error: unknown command " + cmd);
        }
    }
};
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: fxmeasure script.txt\n"); return 2; }
    std::printf ("fxmeasure start\n"); std::fflush (stdout);
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("init done\n"); std::fflush (stdout);
    Host host;
    host.formats.addFormat (new juce::VST3PluginFormat());
    juce::StringArray lines;
    juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])).readLines (lines);
    std::printf ("%d lines\n", lines.size()); std::fflush (stdout);
    host.run (lines);
    host.plugin.reset();
    std::printf ("done\n"); std::fflush (stdout);
    std::_Exit (0); // skip global teardown (some plugins hang there)
}
