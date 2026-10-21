// REA_rodada121: slow plugin modules (a Waves shell) started ahead on their own thread, and the panel's wait for them
// (core/ModuleWarmup.h).

#include <condition_variable>
#include <mutex>

#include "core/ModuleWarmup.h"

TEST (load121_only_waves_shells_start_ahead)
{
    namespace mw = lynx::modulewarm;
    CHECK (mw::worthStartingAhead ("VST3", "C:\\Program Files\\Common Files\\VST3\\WaveShell1-VST3 17.1_x64.vst3"));
    CHECK (mw::worthStartingAhead ("VST3", "C:/Program Files/Common Files/VST3/WaveShell1-VST3-ARA 17.1_x64.vst3"));
    CHECK (mw::worthStartingAhead ("VST3", "/Library/Audio/Plug-Ins/VST3/WaveShell1-VST3 16.0.vst3/")); // a bundle folder
    CHECK (mw::worthStartingAhead ("VST3", "C:\\VST3\\waveshell1-vst3 15.5_x64.VST3"));
    CHECK (! mw::worthStartingAhead ("VST", "C:\\VST\\WaveShell1-VST 17.1_x64.dll"));      // VST2: not this path
    CHECK (! mw::worthStartingAhead ("VST3", "C:\\VST3\\FabFilter Pro-Q 4.vst3"));
    CHECK (! mw::worthStartingAhead ("VST3", "C:\\VST3\\Antares\\Auto-Tune Pro.vst3"));
    CHECK (! mw::worthStartingAhead ("VST3", "C:\\VST3\\WaveShell-readme.txt"));
    CHECK (mw::displayName ("C:\\Program Files\\Common Files\\VST3\\WaveShell1-VST3 17.1_x64.vst3") == "WaveShell1-VST3 17.1");
    CHECK (mw::displayName ("/x/WaveShell1-VST3 16.0.vst3/") == "WaveShell1-VST3 16.0");
}

TEST (load121_registry_starts_each_module_once_and_reports_the_wait)
{
    namespace mw = lynx::modulewarm;
    double now = 100.0;
    int loads = 0;
    bool result = true;
    // not threaded: the loader runs inside start() (a slow, threaded one is the next test)
    mw::Registry quick ([&] (const std::string&) { ++loads; return result; }, [&] { return now; }, false);
    CHECK (quick.start ("a.vst3") == mw::State::ready);
    CHECK (quick.start ("a.vst3") == mw::State::ready);
    CHECK (loads == 1);
    CHECK (quick.waiting().modules == 0);
    result = false;
    CHECK (quick.start ("b.vst3") == mw::State::failed);
    CHECK (quick.start ("b.vst3") == mw::State::failed); // not retried in the same open
    CHECK (loads == 2);
    quick.newOpen();                                      // a later open tries a failed module again
    CHECK (quick.state ("b.vst3") == mw::State::none);
    CHECK (quick.state ("a.vst3") == mw::State::ready);   // a ready one stays ready for the session
    CHECK (quick.start ("b.vst3") == mw::State::failed);
    CHECK (loads == 3);
}

TEST (load121_waiting_module_can_be_skipped_and_comes_back_next_open)
{
    namespace mw = lynx::modulewarm;
    std::mutex m;
    std::condition_variable cv;
    bool release = false;
    std::atomic<double> now { 10.0 };
    mw::Registry reg ([&] (const std::string&) {
        std::unique_lock l (m);
        cv.wait (l, [&] { return release; });
        return true;
    }, [&] { return now.load(); });
    CHECK (reg.start ("C:/VST3/WaveShell1-VST3 17.1_x64.vst3") == mw::State::warming);
    now = 13.0;
    auto w = reg.waiting();
    CHECK (w.modules == 1);
    CHECK (std::abs (w.seconds - 3.0) < 1e-9);
    CHECK (mw::waitTemplate (w) == "Starting {0}...");
    now = 10.0 + mw::Registry::slowSeconds + 1.0;
    w = reg.waiting();
    CHECK (mw::waitTemplate (w).find ("is slow to start") != std::string::npos);
    CHECK (mw::waitTemplate (mw::Waiting {}).empty());

    reg.skipWaiting(); // "Continue without it"
    CHECK (reg.skipped ("C:/VST3/WaveShell1-VST3 17.1_x64.vst3"));
    CHECK (reg.waiting().modules == 0);
    CHECK (reg.start ("C:/VST3/WaveShell1-VST3 17.1_x64.vst3") == mw::State::skipped); // not started twice

    {
        const std::scoped_lock l (m);
        release = true;
    }
    cv.notify_all();
    for (int i = 0; i < 400 && reg.state ("C:/VST3/WaveShell1-VST3 17.1_x64.vst3") == mw::State::skipped; ++i)
    {
        reg.newOpen(); // (only after the thread ended does the module turn ready)
        if (reg.state ("C:/VST3/WaveShell1-VST3 17.1_x64.vst3") == mw::State::ready)
            break;
        reg.skipWaiting();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    reg.newOpen();
    CHECK (reg.state ("C:/VST3/WaveShell1-VST3 17.1_x64.vst3") == mw::State::ready);
    CHECK (reg.waiting().modules == 0);
}

TEST (load121_registry_outlives_nothing_a_thread_still_loading)
{
    namespace mw = lynx::modulewarm;
    std::atomic<bool> go { false }, finished { false };
    {
        mw::Registry reg ([&] (const std::string&) {
            while (! go)
                std::this_thread::yield();
            finished = true;
            return true;
        });
        CHECK (reg.start ("x.vst3") == mw::State::warming);
    } // destroyed while its thread still runs
    go = true;
    for (int i = 0; i < 1000 && ! finished; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    CHECK (finished.load());
}
