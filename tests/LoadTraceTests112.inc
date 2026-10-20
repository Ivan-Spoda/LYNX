// REA_rodada112: opening a project, measured and reported (core/LoadTrace.h) — phase timing, a progress bar that only
// moves forward with "Loading <name>... i/n", and samples on a missing drive / dead network share probed once per root.

#include "core/LoadTrace.h"

TEST (load112_progress_moves_forward_through_stages)
{
    namespace lt = lynx::loadtrace;
    lt::begin ("song.flp");
    CHECK (lt::active());
    CHECK (lt::progress() == 0.0);

    lt::stage (0.30, 0.55, "Reading the samples...", "Reading {0}... {1}/{2}");
    CHECK (std::abs (lt::progress() - 0.30) < 1e-9);
    CHECK (lt::describe (lt::status()) == "Reading the samples...");

    lt::step (2, 4, "take 1.wav");
    CHECK (std::abs (lt::progress() - 0.425) < 1e-9);
    CHECK (lt::describe (lt::status()) == "Reading take 1.wav... 2/4");

    // a late item of an earlier slice never moves the bar back
    lt::stage (0.55, 0.90, "Loading plugins...", "Loading {0}... {1}/{2}");
    lt::step (0, 12, "Auto-Tune Pro", 1);
    CHECK (std::abs (lt::progress() - 0.55) < 1e-9);
    CHECK (lt::describe (lt::status()) == "Loading Auto-Tune Pro... 1/12");
    lt::step (3, 12, "Pro-Q 4", 4);
    CHECK (std::abs (lt::progress() - (0.55 + 0.35 * 3.0 / 12.0)) < 1e-9);
    CHECK (lt::describe (lt::status()) == "Loading Pro-Q 4... 4/12");
    const auto before = lt::progress();
    lt::stage (0.20, 0.30, "Reading the project...");
    CHECK (lt::progress() == before);
    lt::step (1, 1, {});
    CHECK (lt::progress() == before);

    // the template goes through the UI's translation
    lt::stage (0.55, 0.90, "Loading plugins...", "Loading {0}... {1}/{2}");
    lt::step (3, 12, "Pro-Q 4", 4);
    const auto pt = lt::describe (lt::status(), [] (const std::string& s) {
        return s == "Loading {0}... {1}/{2}" ? std::string ("Carregando {0}... {1}/{2}") : s;
    });
    CHECK (pt == "Carregando Pro-Q 4... 4/12");

    lt::end();
    CHECK (! lt::active());
    CHECK (lt::progress() == 1.0);
}

TEST (load112_phases_accumulate_and_report)
{
    namespace lt = lynx::loadtrace;
    lt::begin ("vocal.flp");
    lt::add ("parse", 12.0);
    lt::add ("sample_decode", 100.0);
    lt::add ("sample_decode", 50.0);
    lt::add ("plugin_instantiate", 900.0);
    {
        const lt::Scope s ("rack_build");
    }
    lt::end();
    const auto phases = lt::phases();
    CHECK (phases.size() == 4);
    CHECK (phases[0].name == "parse"); // first-seen order
    CHECK (phases[1].name == "sample_decode" && phases[1].count == 2 && std::abs (phases[1].ms - 150.0) < 1e-9);
    CHECK (phases[3].name == "rack_build" && phases[3].count == 1 && phases[3].ms >= 0.0);

    const auto report = lt::reportLines();
    CHECK (report.find ("load_total_ms=") == 0);
    CHECK (report.find ("load_sample_decode_ms=150\n") != std::string::npos);
    CHECK (report.find ("load_sample_decode_count=2\n") != std::string::npos);
    CHECK (report.find ("load_plugin_instantiate_ms=900\n") != std::string::npos);

    // the log table: slowest first
    const auto table = lt::table();
    CHECK (table.find ("open: vocal.flp") == 0);
    CHECK (table.find ("plugin_instantiate") < table.find ("sample_decode"));
    CHECK (table.find ("sample_decode") < table.find ("parse"));

    // a new trace forgets the last one
    lt::begin ("next.flp");
    CHECK (lt::phases().empty());
    CHECK (lt::progress() == 0.0);
    lt::end();
}

TEST (load112_path_roots)
{
    using lynx::loadtrace::pathRoot;
    CHECK (pathRoot ("K:\\LIWN-EP-ANTIGOS\\vocal\\take 1.wav") == "K:\\");
    CHECK (pathRoot ("k:/LIWN/take.wav") == "K:\\");
    CHECK (pathRoot ("K:") == "K:\\");
    CHECK (pathRoot ("\\\\Studio-PC\\Samples\\Vox\\a.wav") == "\\\\studio-pc\\samples\\");
    CHECK (pathRoot ("\\\\studio-pc\\samples") == "\\\\studio-pc\\samples\\");
    CHECK (pathRoot ("//nas/share/x.wav") == "\\\\nas\\share\\");
    CHECK (pathRoot ("\\\\server") == "");            // no share
    CHECK (pathRoot ("\\\\?\\C:\\long\\path.wav") == ""); // device paths are left to the file check
    CHECK (pathRoot ("Audio\\take.wav") == "");          // relative
    CHECK (pathRoot ("") == "");
    CHECK (pathRoot ("/mnt/k/take.wav") == "/mnt/");
}

TEST (load112_missing_drive_is_probed_once_per_root)
{
    // the user's vocal projects keep their takes on an external drive (K:\) that is not plugged in: every sample
    // path tried several candidate locations, each a file query on the missing drive
    int rootQueries = 0;
    lynx::loadtrace::RootProbe probe ([&rootQueries] (const std::string& root) {
        ++rootQueries;
        return root == "C:\\";
    });
    for (int i = 0; i < 40; ++i)
        CHECK (! probe.rootAvailable ("K:\\LIWN-EP-ANTIGOS\\voz\\take " + std::to_string (i) + ".wav"));
    CHECK (probe.rootAvailable ("C:\\Users\\inliw\\Documents\\Image-Line\\FL Studio\\Projects\\kwe\\Audio\\a.wav"));
    CHECK (probe.rootAvailable ("c:/other.wav"));
    CHECK (! probe.rootAvailable ("\\\\old-pc\\samples\\kick.wav"));
    CHECK (! probe.rootAvailable ("\\\\OLD-PC\\Samples\\snare.wav"));
    CHECK (probe.rootAvailable ("relative\\take.wav")); // not a root question
    CHECK (rootQueries == 3);                            // K:\, C:\, \\old-pc\samples\ — once each
    CHECK (probe.probes() == 3);
    CHECK (probe.skipped() == 39 + 1);
    const auto missing = probe.missingRoots();
    CHECK (missing.size() == 2);

    // in the importer: samples on the missing drive cost no file query at all past the root probe
    flp::Project p;
    for (int i = 0; i < 12; ++i)
        p.channels.push_back ({ i, flp::ChannelType::sampler, "Vox " + std::to_string (i), "", "K:\\LIWN-EP-ANTIGOS\\voz\\take " + std::to_string (i) + ".wav", 1 });
    p.channels.push_back ({ 12, flp::ChannelType::sampler, "Kick", "", "%FLStudioData%\\k.wav", 1 });
    lynx::FlpImportOptions options;
    options.roots = { "C:\\FL", "C:\\FL\\Data", "C:\\User" };
    options.projectFolder = "C:\\Projects\\kwe";
    int fileQueries = 0, kQueries = 0;
    auto roots = std::make_shared<lynx::loadtrace::RootProbe> ([] (const std::string& root) { return root == "C:\\"; });
    options.fileExists = [&, roots] (const std::string& path) {
        if (! roots->rootAvailable (path))
            return false;
        ++fileQueries;
        kQueries += lynx::loadtrace::pathRoot (path) == "K:\\" ? 1 : 0;
        return path == "C:\\FL\\Data\\k.wav";
    };
    lynx::FlpImportReport report;
    options.report = &report;
    const auto s = lynx::importFlp (p, options);
    CHECK (kQueries == 0);
    CHECK (s.channels.size() == 13);
    CHECK (s.channels[12].instrument == lynx::InstrumentKind::sampler);
    CHECK (report.missingFiles.size() == 12); // still reported as missing, for the "Find missing files" dialog
    CHECK (fileQueries > 0);
}
