// LYNX ASIO — a universal ASIO driver on top of Windows Audio (WASAPI), REA round 97.
//
// An in-process COM server implementing Steinberg's IASIO interface. It works with any playback/recording device:
//  * Shared mode: other programs keep their sound. The stream asks Windows for the shortest engine period the device
//    supports (IAudioClient3), falling back to the classic period (10 ms on most devices). The ASIO block size is
//    free: a small FIFO decouples the host's blocks from the device's periods.
//  * Exclusive mode: LYNX ASIO alone on the device, event-driven at the ASIO block size (down to the device's
//    minimum period, often 3 ms), in the best integer/float format the device accepts.
// The host always sees 32-bit float channels. Settings live in %APPDATA%\LYNX\lynx-asio.ini (no registry use except
// DllRegisterServer/DllUnregisterServer, which a user runs once through regsvr32 as administrator).
//
// Clean-room: written from Microsoft's WASAPI documentation and the ASIO SDK interface headers (GPLv3 option). FL Studio
// ASIO was only observed for its general architecture (docs/analise/REA_rodada97.md); none of its code is used.
//
// Build: scripts/build-lynx-asio.sh (Zig/MinGW, x86_64-windows-gnu) -> lynx-asio.dll

#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

#include "InputDrift.h"

// {E987C593-898E-4557-BFE3-4288B0228718} — LYNX ASIO's own class id (hosts also use it as the interface id)
DEFINE_GUID (CLSID_LynxAsio, 0xe987c593, 0x898e, 0x4557, 0xbf, 0xe3, 0x42, 0x88, 0xb0, 0x22, 0x87, 0x18);

namespace
{
constexpr const char* driverName = "LYNX ASIO";
constexpr const wchar_t* driverNameW = L"LYNX ASIO";
constexpr long driverVersion = 1;

HINSTANCE moduleHandle = nullptr;
std::atomic<long> serverLocks { 0 }, liveObjects { 0 };

//==============================================================================================================
// Small helpers

template <class T>
void releaseCom (T*& p)
{
    if (p != nullptr)
        p->Release();
    p = nullptr;
}

double nowMs()
{
    static const double toMs = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency (&f);
        return 1000.0 / (double) f.QuadPart;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter (&c);
    return (double) c.QuadPart * toMs;
}

bool portugueseUi() { return PRIMARYLANGID (GetUserDefaultUILanguage()) == LANG_PORTUGUESE; }

// English text, or its pt-BR translation when Windows speaks Portuguese.
const wchar_t* tr (const wchar_t* en, const wchar_t* pt) { return portugueseUi() ? pt : en; }

std::string narrow (const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte (CP_ACP, 0, w.c_str(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) n, '\0');
    WideCharToMultiByte (CP_ACP, 0, w.c_str(), (int) w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Runs `f` on a fresh thread inside the multithreaded COM apartment and waits for it: every WASAPI object of this
// driver lives in the MTA, whatever apartment the host calls from.
template <class F>
DWORD WINAPI mtaThreadMain (LPVOID p) // WINAPI: thread entry points are __stdcall on 32-bit Windows
{
    const auto hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    (*(F*) p)();
    if (SUCCEEDED (hr))
        CoUninitialize();
    return 0;
}

template <class F>
void onMtaThread (F& f)
{
    HANDLE t = CreateThread (nullptr, 0, &mtaThreadMain<F>, &f, 0, nullptr);
    if (t != nullptr)
    {
        WaitForSingleObject (t, INFINITE);
        CloseHandle (t);
    }
}

//==============================================================================================================
// Settings (%APPDATA%\LYNX\lynx-asio.ini; LYNX_ASIO_INI overrides the path for tests)

enum class Mode
{
    shared = 0,
    exclusive = 1
};

struct Settings
{
    Mode mode = Mode::shared;
    int bufferSize = 0;     // the preferred ASIO block in frames; 0 = automatic
    int safety = 2;         // shared mode: 0 = one engine period queued, 1 = 1.5 periods, 2 = two periods
    std::wstring output;    // endpoint id; empty = Windows' default device
    std::wstring input;     // endpoint id; empty = default; "-" = no input

    static std::wstring path()
    {
        wchar_t buf[MAX_PATH * 2] {};
        if (GetEnvironmentVariableW (L"LYNX_ASIO_INI", buf, MAX_PATH * 2) > 0)
            return buf;
        if (GetEnvironmentVariableW (L"APPDATA", buf, MAX_PATH * 2) == 0)
            return {};
        std::wstring dir = std::wstring (buf) + L"\\LYNX";
        CreateDirectoryW (dir.c_str(), nullptr);
        return dir + L"\\lynx-asio.ini";
    }

    static Settings load()
    {
        Settings s;
        const auto file = path();
        if (file.empty())
            return s;
        const auto* f = file.c_str();
        s.mode = GetPrivateProfileIntW (L"LYNX ASIO", L"mode", 0, f) == 1 ? Mode::exclusive : Mode::shared;
        s.bufferSize = std::clamp ((int) GetPrivateProfileIntW (L"LYNX ASIO", L"bufferSize", 0, f), 0, 8192);
        s.safety = std::clamp ((int) GetPrivateProfileIntW (L"LYNX ASIO", L"safety", 2, f), 0, 2);
        wchar_t buf[512] {};
        GetPrivateProfileStringW (L"LYNX ASIO", L"output", L"", buf, 512, f);
        s.output = buf;
        GetPrivateProfileStringW (L"LYNX ASIO", L"input", L"", buf, 512, f);
        s.input = buf;
        return s;
    }

    void save() const
    {
        const auto file = path();
        if (file.empty())
            return;
        const auto* f = file.c_str();
        WritePrivateProfileStringW (L"LYNX ASIO", L"mode", mode == Mode::exclusive ? L"1" : L"0", f);
        WritePrivateProfileStringW (L"LYNX ASIO", L"bufferSize", std::to_wstring (bufferSize).c_str(), f);
        WritePrivateProfileStringW (L"LYNX ASIO", L"safety", std::to_wstring (safety).c_str(), f);
        WritePrivateProfileStringW (L"LYNX ASIO", L"output", output.c_str(), f);
        WritePrivateProfileStringW (L"LYNX ASIO", L"input", input.c_str(), f);
    }

    bool operator== (const Settings& o) const
    {
        return mode == o.mode && bufferSize == o.bufferSize && safety == o.safety && output == o.output && input == o.input;
    }
};

// Optional log (LYNX_ASIO_LOG = file path): what was opened and the stream statistics. Never on the audio path.
void logLine (const char* fmt, ...)
{
    wchar_t path[MAX_PATH * 2] {};
    if (GetEnvironmentVariableW (L"LYNX_ASIO_LOG", path, MAX_PATH * 2) == 0)
        return;
    char text[1024];
    va_list args;
    va_start (args, fmt);
    vsnprintf (text, sizeof (text), fmt, args);
    va_end (args);
    if (FILE* f = _wfopen (path, L"a"))
    {
        fprintf (f, "%s\n", text);
        fclose (f);
    }
}

//==============================================================================================================
// Endpoints and formats

struct Endpoint
{
    std::wstring id, name;
};

std::vector<Endpoint> listEndpoints (EDataFlow flow)
{
    std::vector<Endpoint> result;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)))
        return result;
    IMMDeviceCollection* all = nullptr;
    if (SUCCEEDED (en->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE, &all)))
    {
        UINT n = 0;
        all->GetCount (&n);
        for (UINT i = 0; i < n; ++i)
        {
            IMMDevice* d = nullptr;
            if (FAILED (all->Item (i, &d)))
                continue;
            Endpoint e;
            LPWSTR id = nullptr;
            if (SUCCEEDED (d->GetId (&id)))
            {
                e.id = id;
                CoTaskMemFree (id);
            }
            IPropertyStore* props = nullptr;
            if (SUCCEEDED (d->OpenPropertyStore (STGM_READ, &props)))
            {
                PROPVARIANT v;
                PropVariantInit (&v);
                if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                    e.name = v.pwszVal;
                PropVariantClear (&v);
                props->Release();
            }
            result.push_back (e);
            d->Release();
        }
        all->Release();
    }
    en->Release();
    return result;
}

// The chosen endpoint (empty id = Windows' default device for that direction); nullptr when it is gone.
IMMDevice* openEndpoint (EDataFlow flow, const std::wstring& id)
{
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)))
        return nullptr;
    IMMDevice* d = nullptr;
    if (! id.empty())
        en->GetDevice (id.c_str(), &d);
    if (d == nullptr)
        en->GetDefaultAudioEndpoint (flow, eConsole, &d);
    en->Release();
    return d;
}

std::wstring endpointName (IMMDevice* d)
{
    std::wstring name;
    IPropertyStore* props = nullptr;
    if (d != nullptr && SUCCEEDED (d->OpenPropertyStore (STGM_READ, &props)))
    {
        PROPVARIANT v;
        PropVariantInit (&v);
        if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            name = v.pwszVal;
        PropVariantClear (&v);
        props->Release();
    }
    return name;
}

enum class SampleKind
{
    float32,
    int32,
    int24,
    int16
};

struct StreamFormat
{
    WAVEFORMATEXTENSIBLE wf {};
    SampleKind kind = SampleKind::float32;
    int channels = 2;
    int bytesPerFrame = 8;
};

StreamFormat makeFormat (int channels, int rate, SampleKind kind, DWORD channelMask)
{
    StreamFormat f;
    f.kind = kind;
    f.channels = channels;
    const int container = kind == SampleKind::int16 ? 16 : kind == SampleKind::int24 ? 24 : 32;
    const int valid = kind == SampleKind::int16 ? 16 : kind == SampleKind::int24 ? 24 : 32;
    auto& w = f.wf;
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels = (WORD) channels;
    w.Format.nSamplesPerSec = (DWORD) rate;
    w.Format.wBitsPerSample = (WORD) container;
    w.Format.nBlockAlign = (WORD) (channels * container / 8);
    w.Format.nAvgBytesPerSec = w.Format.nBlockAlign * (DWORD) rate;
    w.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
    w.Samples.wValidBitsPerSample = (WORD) valid;
    w.dwChannelMask = channelMask;
    w.SubFormat = kind == SampleKind::float32 ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    f.bytesPerFrame = w.Format.nBlockAlign;
    return f;
}

DWORD maskOf (const WAVEFORMATEX* w)
{
    if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return ((const WAVEFORMATEXTENSIBLE*) w)->dwChannelMask;
    return w->nChannels == 1 ? SPEAKER_FRONT_CENTER : w->nChannels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
}

const char* kindName (SampleKind k)
{
    switch (k)
    {
        case SampleKind::float32: return "float32";
        case SampleKind::int32:   return "int32";
        case SampleKind::int24:   return "int24";
        case SampleKind::int16:   return "int16";
    }
    return "?";
}

// interleaved float frames -> device bytes
void writeSamples (const float* src, BYTE* dst, int samples, SampleKind kind)
{
    switch (kind)
    {
        case SampleKind::float32:
            std::memcpy (dst, src, (size_t) samples * 4);
            break;
        case SampleKind::int32:
            for (int i = 0; i < samples; ++i)
            {
                const double v = std::clamp ((double) src[i], -1.0, 1.0) * 2147483647.0;
                ((int32_t*) dst)[i] = (int32_t) std::lrint (v);
            }
            break;
        case SampleKind::int24:
            for (int i = 0; i < samples; ++i)
            {
                const auto v = (int32_t) std::lrint (std::clamp (src[i], -1.0f, 1.0f) * 8388607.0f);
                dst[3 * i] = (BYTE) (v & 0xff);
                dst[3 * i + 1] = (BYTE) ((v >> 8) & 0xff);
                dst[3 * i + 2] = (BYTE) ((v >> 16) & 0xff);
            }
            break;
        case SampleKind::int16:
            for (int i = 0; i < samples; ++i)
                ((int16_t*) dst)[i] = (int16_t) std::lrint (std::clamp (src[i], -1.0f, 1.0f) * 32767.0f);
            break;
    }
}

// device bytes -> interleaved float frames
void readSamples (const BYTE* src, float* dst, int samples, SampleKind kind)
{
    switch (kind)
    {
        case SampleKind::float32:
            std::memcpy (dst, src, (size_t) samples * 4);
            break;
        case SampleKind::int32:
            for (int i = 0; i < samples; ++i)
                dst[i] = (float) (((const int32_t*) src)[i] / 2147483648.0);
            break;
        case SampleKind::int24:
            for (int i = 0; i < samples; ++i)
            {
                const int32_t v = (int32_t) ((uint32_t) src[3 * i] << 8 | (uint32_t) src[3 * i + 1] << 16 | (uint32_t) src[3 * i + 2] << 24) >> 8;
                dst[i] = (float) v / 8388608.0f;
            }
            break;
        case SampleKind::int16:
            for (int i = 0; i < samples; ++i)
                dst[i] = (float) ((const int16_t*) src)[i] / 32768.0f;
            break;
    }
}

REFERENCE_TIME framesToRefTime (int frames, double rate) { return (REFERENCE_TIME) std::llround (frames * 1.0e7 / rate); }
int refTimeToFrames (REFERENCE_TIME t, double rate) { return (int) std::llround ((double) t * rate / 1.0e7); }

const double standardRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

// What a device offers (probed in init and by the panel).
struct DeviceInfo
{
    bool ok = false;
    std::wstring name;
    int channels = 2;
    double mixRate = 48000.0;
    DWORD channelMask = 0;
    int sharedPeriod = 480;           // frames at mixRate: the shortest shared engine period
    int classicPeriod = 480;          // frames: Windows' default device period
    int exclusiveMinPeriod = 0;       // frames at mixRate (0 = exclusive mode unavailable)
    std::vector<double> exclusiveRates;
};

// Exclusive-mode format for `rate`, best first; false when the device takes none.
bool exclusiveFormat (IMMDevice* d, int channels, int rate, DWORD mask, StreamFormat& out)
{
    IAudioClient* c = nullptr;
    if (FAILED (d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &c)))
        return false;
    bool found = false;
    for (auto kind : { SampleKind::float32, SampleKind::int32, SampleKind::int24, SampleKind::int16 })
    {
        auto f = makeFormat (channels, rate, kind, mask);
        if (c->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE, &f.wf.Format, nullptr) == S_OK)
        {
            out = f;
            found = true;
            break;
        }
        if (kind == SampleKind::int32) // 24 valid bits in a 32-bit container
        {
            f.wf.Samples.wValidBitsPerSample = 24;
            if (c->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE, &f.wf.Format, nullptr) == S_OK)
            {
                out = f;
                found = true;
                break;
            }
        }
    }
    c->Release();
    return found;
}

DeviceInfo probeDevice (EDataFlow flow, const std::wstring& id)
{
    DeviceInfo info;
    IMMDevice* d = openEndpoint (flow, id);
    if (d == nullptr)
        return info;
    info.name = endpointName (d);
    IAudioClient* c = nullptr;
    if (SUCCEEDED (d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &c)))
    {
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED (c->GetMixFormat (&mix)))
        {
            info.ok = true;
            info.channels = mix->nChannels;
            info.mixRate = mix->nSamplesPerSec;
            info.channelMask = maskOf (mix);
            REFERENCE_TIME def = 0, mn = 0;
            if (SUCCEEDED (c->GetDevicePeriod (&def, &mn)))
            {
                info.classicPeriod = info.sharedPeriod = refTimeToFrames (def, info.mixRate);
                info.exclusiveMinPeriod = refTimeToFrames (mn, info.mixRate);
            }
            IAudioClient3* c3 = nullptr;
            if (SUCCEEDED (c->QueryInterface (__uuidof (IAudioClient3), (void**) &c3)))
            {
                UINT32 dp = 0, fp = 0, lo = 0, hi = 0;
                if (SUCCEEDED (c3->GetSharedModeEnginePeriod (mix, &dp, &fp, &lo, &hi)) && lo > 0)
                    info.sharedPeriod = (int) lo;
                c3->Release();
            }
            CoTaskMemFree (mix);
        }
        c->Release();
    }
    if (info.ok) // exclusive mode: the rates (and the minimum period) of the playback and of the recording device
    {
        StreamFormat f;
        for (auto r : standardRates)
            if (exclusiveFormat (d, info.channels, (int) r, info.channelMask, f))
                info.exclusiveRates.push_back (r);
        if (info.exclusiveRates.empty())
            info.exclusiveMinPeriod = 0;
    }
    d->Release();
    return info;
}

//==============================================================================================================
// Interleaved float FIFO (used by the audio thread only)

struct Fifo
{
    std::vector<float> data;
    int channels = 0, capacity = 0, head = 0, count = 0;

    void reset (int ch, int frames)
    {
        channels = ch;
        capacity = std::max (1, frames);
        data.assign ((size_t) (capacity * std::max (1, ch)), 0.0f);
        head = count = 0;
    }
    int space() const { return capacity - count; }
    void push (const float* src, int frames) // drops the oldest frames on overflow
    {
        for (int i = 0; i < frames; ++i)
        {
            if (count == capacity)
            {
                head = (head + 1) % capacity;
                --count;
            }
            const int at = (head + count) % capacity;
            std::memcpy (&data[(size_t) (at * channels)], src + (size_t) i * channels, sizeof (float) * (size_t) channels);
            ++count;
        }
    }
    void pushSilence (int frames)
    {
        std::vector<float> zero ((size_t) (frames * channels), 0.0f);
        push (zero.data(), frames);
    }
    int pop (float* dst, int frames) // returns the frames really taken; the rest of dst is zeroed
    {
        const int n = std::min (frames, count);
        for (int i = 0; i < n; ++i)
        {
            std::memcpy (dst + (size_t) i * channels, &data[(size_t) (head * channels)], sizeof (float) * (size_t) channels);
            head = (head + 1) % capacity;
        }
        count -= n;
        if (n < frames)
            std::memset (dst + (size_t) n * channels, 0, sizeof (float) * (size_t) ((frames - n) * channels));
        return n;
    }
    void drop (int frames)
    {
        const int n = std::min (frames, count);
        head = (head + n) % capacity;
        count -= n;
    }
};

//==============================================================================================================

class LynxAsio;
INT_PTR showPanel (LynxAsio& driver, HWND owner);

class LynxAsio final : public IASIO
{
public:
    LynxAsio() { ++liveObjects; }
    ~LynxAsio()
    {
        disposeBuffers();
        --liveObjects;
    }

    // IUnknown --------------------------------------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;
        // ASIO hosts ask for the driver's own CLSID as the interface id
        if (IsEqualIID (iid, IID_IUnknown) || IsEqualIID (iid, CLSID_LynxAsio))
        {
            *out = static_cast<IASIO*> (this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG) ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto n = --refs;
        if (n == 0)
            delete this;
        return (ULONG) n;
    }

    // IASIO -----------------------------------------------------------------------------------------------------
    ASIOBool init (void* sysHandle) override
    {
        owner = (HWND) sysHandle;
        settings = Settings::load();
        probe();
        if (! render.ok)
        {
            setError ("No playback device is available.");
            return ASIOFalse;
        }
        sampleRate = settings.mode == Mode::exclusive && ! render.exclusiveRates.empty()
                         && std::find (render.exclusiveRates.begin(), render.exclusiveRates.end(), render.mixRate) == render.exclusiveRates.end()
                         ? render.exclusiveRates.front() : render.mixRate;
        initialised = true;
        return ASIOTrue;
    }

    void getDriverName (char* name) override { std::strcpy (name, driverName); }
    long getDriverVersion() override { return driverVersion; }
    void getErrorMessage (char* text) override { std::strncpy (text, error, 123); text[123] = 0; }

    ASIOError start() override
    {
        if (thread == nullptr)
            return ASE_NotPresent;
        if (running)
            return ASE_OK;
        return command (Command::start) ? (running = true, ASE_OK) : ASE_HWMalfunction;
    }

    ASIOError stop() override
    {
        if (thread == nullptr)
            return ASE_NotPresent;
        if (running)
            command (Command::stop);
        running = false;
        return ASE_OK;
    }

    ASIOError getChannels (long* numIn, long* numOut) override
    {
        if (! initialised)
            return ASE_NotPresent;
        *numIn = inputCount();
        *numOut = render.channels;
        return ASE_OK;
    }

    ASIOError getLatencies (long* in, long* out) override
    {
        if (! initialised)
            return ASE_NotPresent;
        const auto l = latencies (blockSize > 0 ? blockSize : preferredBlock());
        *in = l.input;
        *out = l.output;
        return ASE_OK;
    }

    ASIOError getBufferSize (long* minSize, long* maxSize, long* preferred, long* granularity) override
    {
        if (! initialised)
            return ASE_NotPresent;
        *minSize = exclusiveActive() ? std::max (16, render.exclusiveMinPeriod) : 32;
        *maxSize = 4096;
        *preferred = preferredBlock();
        *granularity = -1; // powers of two (and the preferred size)
        return ASE_OK;
    }

    ASIOError canSampleRate (ASIOSampleRate rate) override
    {
        if (! initialised)
            return ASE_NotPresent;
        if (exclusiveActive())
            return std::find (render.exclusiveRates.begin(), render.exclusiveRates.end(), rate) != render.exclusiveRates.end() ? ASE_OK : ASE_NoClock;
        // shared: Windows converts to the mix rate when it differs
        return std::find (std::begin (standardRates), std::end (standardRates), rate) != std::end (standardRates) ? ASE_OK : ASE_NoClock;
    }

    ASIOError getSampleRate (ASIOSampleRate* rate) override
    {
        if (! initialised)
            return ASE_NotPresent;
        *rate = sampleRate;
        return ASE_OK;
    }

    ASIOError setSampleRate (ASIOSampleRate rate) override
    {
        if (canSampleRate (rate) != ASE_OK)
            return ASE_NoClock;
        if (rate != sampleRate)
        {
            sampleRate = rate;
            if (callbacks != nullptr) // buffers exist: the host has to rebuild them at the new rate
                callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr);
        }
        return ASE_OK;
    }

    ASIOError getClockSources (ASIOClockSource* clocks, long* count) override
    {
        if (clocks == nullptr || count == nullptr || *count < 1)
            return ASE_InvalidParameter;
        clocks[0].index = 0;
        clocks[0].associatedChannel = -1;
        clocks[0].associatedGroup = -1;
        clocks[0].isCurrentSource = ASIOTrue;
        std::strcpy (clocks[0].name, "Internal");
        *count = 1;
        return ASE_OK;
    }

    ASIOError setClockSource (long reference) override { return reference == 0 ? ASE_OK : ASE_InvalidParameter; }

    ASIOError getSamplePosition (ASIOSamples* pos, ASIOTimeStamp* stamp) override
    {
        if (thread == nullptr)
            return ASE_NotPresent;
        const auto frames = (unsigned long long) lastPosition.load();
        const auto ns = (unsigned long long) lastSystemNs.load();
        pos->hi = (unsigned long) (frames >> 32);
        pos->lo = (unsigned long) (frames & 0xffffffffu);
        stamp->hi = (unsigned long) (ns >> 32);
        stamp->lo = (unsigned long) (ns & 0xffffffffu);
        return ASE_OK;
    }

    ASIOError getChannelInfo (ASIOChannelInfo* info) override
    {
        if (! initialised)
            return ASE_NotPresent;
        const long count = info->isInput ? inputCount() : render.channels;
        if (info->channel < 0 || info->channel >= count)
            return ASE_InvalidParameter;
        info->isActive = ASIOFalse;
        for (auto& c : channels)
            if (c.isInput == (info->isInput != ASIOFalse) && c.index == info->channel)
                info->isActive = ASIOTrue;
        info->channelGroup = 0;
        info->type = ASIOSTFloat32LSB;
        const auto& dev = info->isInput ? capture : render;
        auto label = narrow (dev.name);
        if (label.size() > 20)
            label.resize (20);
        std::snprintf (info->name, sizeof (info->name), "%s %ld", label.empty() ? (info->isInput ? "In" : "Out") : label.c_str(), info->channel + 1);
        return ASE_OK;
    }

    ASIOError createBuffers (ASIOBufferInfo* infos, long count, long size, ASIOCallbacks* cbs) override
    {
        if (! initialised)
            return ASE_NotPresent;
        if (thread != nullptr)
            disposeBuffers();
        if (infos == nullptr || cbs == nullptr || size < 16 || size > 8192)
            return ASE_InvalidMode;
        channels.clear();
        for (long i = 0; i < count; ++i)
        {
            const bool isInput = infos[i].isInput != ASIOFalse;
            if (infos[i].channelNum < 0 || infos[i].channelNum >= (isInput ? inputCount() : render.channels))
                return ASE_InvalidParameter;
            channels.push_back ({ isInput, infos[i].channelNum });
        }
        blockSize = (int) size;
        memory.assign ((size_t) (count * 2 * size), 0.0f);
        for (long i = 0; i < count; ++i)
        {
            channels[(size_t) i].buffers[0] = &memory[(size_t) (i * 2 * size)];
            channels[(size_t) i].buffers[1] = &memory[(size_t) (i * 2 * size + size)];
            infos[i].buffers[0] = channels[(size_t) i].buffers[0];
            infos[i].buffers[1] = channels[(size_t) i].buffers[1];
        }
        callbacks = cbs;
        useTimeInfo = callbacks->asioMessage (kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
                      && callbacks->asioMessage (kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
        hostKnowsLatencyChanges = callbacks->asioMessage (kAsioSelectorSupported, kAsioLatenciesChanged, nullptr, nullptr) == 1;

        // the audio thread opens the devices now (so getLatencies is exact) and waits for start()
        wakeEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        doneEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        thread = CreateThread (nullptr, 0, &audioThreadMain, this, 0, nullptr);
        WaitForSingleObject (doneEvent, 10000);
        if (! openOk)
        {
            disposeBuffers();
            return ASE_HWMalfunction;
        }
        return ASE_OK;
    }

    ASIOError disposeBuffers() override
    {
        if (thread != nullptr)
        {
            stop();
            command (Command::quit);
            WaitForSingleObject (thread, 5000);
            CloseHandle (thread);
            thread = nullptr;
        }
        for (auto* h : { &wakeEvent, &doneEvent })
            if (*h != nullptr)
            {
                CloseHandle (*h);
                *h = nullptr;
            }
        callbacks = nullptr;
        channels.clear();
        memory.clear();
        blockSize = 0;
        deviceQueue = 0;
        return ASE_OK;
    }

    ASIOError controlPanel() override
    {
        const auto before = settings;
        showPanel (*this, owner);
        if (! (settings == before))
        {
            settings.save();
            probe();
            if (callbacks != nullptr)
                callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr);
        }
        return ASE_OK;
    }

    ASIOError future (long selector, void*) override
    {
        switch (selector)
        {
            case kAsioCanTimeInfo:
                return ASE_SUCCESS;
            default:
                return ASE_NotPresent;
        }
    }

    ASIOError outputReady() override { return ASE_NotPresent; }

    // for the panel ----------------------------------------------------------------------------------------------
    Settings settings;
    DeviceInfo render, capture;

    void probe()
    {
        auto work = [this]
        {
            render = probeDevice (eRender, settings.output);
            capture = settings.input == L"-" ? DeviceInfo {} : probeDevice (eCapture, settings.input);
        };
        onMtaThread (work);
    }

    int preferredBlock() const
    {
        if (settings.bufferSize > 0)
            return exclusiveActive() ? std::max (settings.bufferSize, render.exclusiveMinPeriod) : settings.bufferSize;
        if (exclusiveActive())
            return std::max (render.exclusiveMinPeriod, 128);
        return std::max (32, render.sharedPeriod);
    }

    int sharedTarget (int period) const { return period + (settings.safety == 0 ? 0 : settings.safety == 1 ? period / 2 : period); }

    // The input FIFO's level after a host block (frames). The recorded packets (`capture` frames) and the playback
    // bursts (whole blocks covering one device period) come at unrelated moments; just before a burst the FIFO must
    // still hold the whole burst plus the interpolator's look-ahead, whatever the phase between the two. Worst case:
    // target + (burst + block) / 2 - capture >= burst + look-ahead, plus a margin for late packets that follows the
    // safety setting. Measured (round 106, USB codec): packets of a 6 ms exclusive stream came up to 11.8 ms apart,
    // of the 10 ms shared stream up to 16.6 ms apart, so "normal" allows one period (at least 6 ms) of lateness.
    // A device that is later still raises the target once (InputDrift, at most 2x).
    int inputTargetFor (int capturePacket, int period, int block) const
    {
        const int b = std::max (1, block);
        const int burst = (std::max (period, b) + b - 1) / b * b;
        const int p = std::max (capturePacket, burst);
        const double ms = sampleRate / 1000.0;
        const int late = settings.safety == 0 ? std::max (p / 4, (int) (2 * ms))
                       : settings.safety == 1 ? std::max (p / 2, (int) (4 * ms))
                                              : std::max (p, (int) (6 * ms));
        return capturePacket + burst / 2 - b / 2 + lynxasio::InputDrift::half + 32 + late;
    }

    // Exclusive recording period: the playback period, but not below twice the device's minimum. Measured on a USB
    // class-driver codec (REA round 106): at its 3 ms minimum the capture events arrive up to 8 ms apart now and
    // then and the device drops a packet each time (about one per second); from 6 ms on there were none.
    int exclusiveCapturePeriod (int playbackPeriod) const { return std::max (playbackPeriod, 2 * capture.exclusiveMinPeriod); }

    struct Latencies
    {
        int block = 0, input = 0, output = 0, period = 0, capturePeriod = 0;
        bool open = false, exclusive = false, captureExclusive = false, hasInput = false;
    };

    // ASIO latencies for `block`: of the open stream when there is one, else what the settings would give.
    // Output: what is queued ahead of the device (the shared target or the exclusive period) plus the block waiting
    // in the FIFO. Input: the FIFO level when the host reads a block (target + block), plus half a capture packet
    // (the average age of the newest frame) and the interpolator's look-ahead.
    Latencies latencies (int block, bool ofOpenStream = true) const
    {
        Latencies l;
        l.block = block;
        if (ofOpenStream && thread != nullptr && openOk)
        {
            l.open = true;
            l.exclusive = streamExclusive;
            l.period = devicePeriod;
            l.output = deviceQueue + blockSize;
            l.block = blockSize;
            l.hasInput = captureService != nullptr;
            if (l.hasInput)
            {
                l.captureExclusive = captureExclusive;
                l.capturePeriod = capturePeriod;
                l.input = std::max (inputTarget, inFifo.target()) + blockSize + capturePeriod / 2 + lynxasio::InputDrift::half;
            }
            return l;
        }
        l.exclusive = exclusiveActive();
        l.period = l.exclusive ? std::max (block, render.exclusiveMinPeriod) : render.sharedPeriod;
        l.output = (l.exclusive ? l.period : sharedTarget (l.period)) + block;
        l.hasInput = capture.ok;
        if (capture.ok)
        {
            l.captureExclusive = l.exclusive && capture.exclusiveMinPeriod > 0;
            l.capturePeriod = l.captureExclusive ? exclusiveCapturePeriod (l.period) : capture.sharedPeriod;
            l.input = inputTargetFor (l.capturePeriod, l.period, block) + block + l.capturePeriod / 2 + lynxasio::InputDrift::half;
        }
        return l;
    }
    bool exclusiveActive() const { return settings.mode == Mode::exclusive && render.exclusiveMinPeriod > 0; }

private:
    enum class Command
    {
        none,
        start,
        stop,
        quit
    };

    struct Channel
    {
        bool isInput = false;
        long index = 0;
        float* buffers[2] { nullptr, nullptr };
    };

    long inputCount() const { return capture.ok ? capture.channels : 0; }

    void setError (const char* text)
    {
        std::strncpy (error, text, sizeof (error) - 1);
        logLine ("error: %s", text);
    }

    bool command (Command c)
    {
        pending = c;
        commandOk = false;
        SetEvent (wakeEvent);
        return WaitForSingleObject (doneEvent, 5000) == WAIT_OBJECT_0 && commandOk;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Audio thread: opens the clients, then serves start/stop/quit and the device events.

    IAudioClient* renderClient = nullptr;
    IAudioRenderClient* renderService = nullptr;
    IAudioClient* captureClient = nullptr;
    IAudioCaptureClient* captureService = nullptr;
    HANDLE renderEvent = nullptr, captureEvent = nullptr;
    StreamFormat renderFormat, captureFormat;
    UINT32 renderFrames = 0;   // the render buffer
    int devicePeriod = 0;      // frames between two device events
    Fifo outFifo;
    lynxasio::InputDrift inFifo; // captured frames -> host blocks, following the capture clock
    std::vector<float> scratch, blockIn, blockOut;
    int bufferIndex = 0;
    long long position = 0;

    // statistics (LYNX_ASIO_LOG)
    long long events = 0, hostBlocks = 0, lateEvents = 0, slowBursts = 0, captureEvents = 0, captureGaps = 0;
    long long inputShortBefore = 0;
    double startMs = 0.0, lastEventMs = 0.0, maxGapMs = 0.0, maxBurstMs = 0.0;
    struct TracePoint
    {
        float level, ppm;
        int underflows, gaps;
    };
    TracePoint trace[240] {}; // input state once per second (first 4 minutes), written to the log at stop
    int traced = 0;
    struct GapNote
    {
        float atMs, sinceDrainMs;
        int frames, packetIndex;
    };
    GapNote gapNotes[8] {};
    double lastDrainMs = 0.0, maxCaptureGapMs = 0.0;
    bool inputSettled = false;
    int packetsThisDrain = 0;

    // Opens an exclusive event-driven stream of `period` frames (re-initialised with the aligned size when the
    // device asks for it). Returns the HRESULT.
    static HRESULT initExclusive (IMMDevice* d, IAudioClient*& client, const StreamFormat& format, int period, double rate)
    {
        auto t = framesToRefTime (period, rate);
        HRESULT hr = client->Initialize (AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, t, t, &format.wf.Format, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            UINT32 aligned = 0;
            client->GetBufferSize (&aligned);
            releaseCom (client);
            if (FAILED (d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client)))
                return E_FAIL;
            t = framesToRefTime ((int) aligned, rate);
            hr = client->Initialize (AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, t, t, &format.wf.Format, nullptr);
        }
        return hr;
    }

    // Shared event-driven stream; the IAudioClient3 low-latency period when the device has one shorter than its
    // default and no rate conversion is needed. Returns the period in frames (0 = failed).
    static int initShared (IAudioClient* client, const StreamFormat& format, double rate, double mixRate, REFERENCE_TIME buffer)
    {
        const bool atMixRate = rate == mixRate;
        IAudioClient3* c3 = nullptr;
        int period = 0;
        if (atMixRate && SUCCEEDED (client->QueryInterface (__uuidof (IAudioClient3), (void**) &c3)))
        {
            UINT32 dp = 0, fp = 0, lo = 0, hi = 0;
            if (SUCCEEDED (c3->GetSharedModeEnginePeriod (&format.wf.Format, &dp, &fp, &lo, &hi)) && lo > 0 && lo < dp
                && SUCCEEDED (c3->InitializeSharedAudioStream (AUDCLNT_STREAMFLAGS_EVENTCALLBACK, lo, &format.wf.Format, nullptr)))
                period = (int) lo;
            c3->Release();
        }
        if (period == 0) // the classic shared stream (also converts other rates to the mix rate)
        {
            DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
            if (! atMixRate)
                flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            if (FAILED (client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, buffer, 0, &format.wf.Format, nullptr)))
                return 0;
            REFERENCE_TIME def = 0, mn = 0;
            client->GetDevicePeriod (&def, &mn);
            period = std::max (16, refTimeToFrames (def, rate));
        }
        return period;
    }

    bool openRender (IMMDevice* d)
    {
        const bool exclusive = streamExclusive = exclusiveActive(); // fixed for this stream (the panel may change the settings)
        devicePeriod = 0;
        renderFrames = 0;
        if (FAILED (d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &renderClient)))
            return setError ("Could not open the playback device."), false;

        if (exclusive)
        {
            if (! exclusiveFormat (d, render.channels, (int) sampleRate, render.channelMask, renderFormat))
                return setError ("The playback device takes no exclusive format at this sample rate."), false;
            const HRESULT hr = initExclusive (d, renderClient, renderFormat, std::max (blockSize, render.exclusiveMinPeriod), sampleRate);
            if (hr == AUDCLNT_E_DEVICE_IN_USE)
                return setError ("Another program is using the playback device in exclusive mode."), false;
            if (FAILED (hr))
                return setError ("The playback device refused exclusive mode."), false;
            renderClient->GetBufferSize (&renderFrames);
            devicePeriod = (int) renderFrames;
        }
        else
        {
            renderFormat = makeFormat (render.channels, (int) sampleRate, SampleKind::float32, render.channelMask);
            devicePeriod = initShared (renderClient, renderFormat, sampleRate, render.mixRate, 0);
            if (devicePeriod == 0)
                return setError ("The playback device refused the shared stream."), false;
            renderClient->GetBufferSize (&renderFrames);
        }
        renderEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        renderClient->SetEventHandle (renderEvent);
        if (FAILED (renderClient->GetService (__uuidof (IAudioRenderClient), (void**) &renderService)))
            return setError ("The playback device has no render service."), false;
        deviceQueue = exclusive ? (int) renderFrames : std::min ((int) renderFrames, sharedTarget (devicePeriod));
        REFERENCE_TIME lat = 0;
        renderClient->GetStreamLatency (&lat);
        renderLatency = refTimeToFrames (lat, sampleRate);
        logLine ("render: %s mode, %s %d ch @ %.0f Hz, period %d, buffer %u, queue %d, stream latency %d, ASIO block %d",
                 exclusive ? "exclusive" : "shared", kindName (renderFormat.kind), renderFormat.channels, sampleRate, devicePeriod,
                 renderFrames, deviceQueue, renderLatency, blockSize);
        return true;
    }

    // The recording stream: exclusive (event-driven, same period as the playback when the device allows it) when
    // LYNX ASIO runs exclusive, else shared. Either way it has its own event and clock; InputDrift follows it.
    void openCapture()
    {
        captureExclusive = false;
        capturePeriod = 0;
        if (! capture.ok)
            return;
        IMMDevice* d = openEndpoint (eCapture, settings.input);
        if (d == nullptr)
            return;
        if (streamExclusive && capture.exclusiveMinPeriod > 0
            && exclusiveFormat (d, capture.channels, (int) sampleRate, capture.channelMask, captureFormat)
            && SUCCEEDED (d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &captureClient)))
        {
            const HRESULT hr = initExclusive (d, captureClient, captureFormat, exclusiveCapturePeriod (devicePeriod), sampleRate);
            UINT32 frames = 0;
            if (SUCCEEDED (hr) && SUCCEEDED (captureClient->GetBufferSize (&frames)))
            {
                captureExclusive = true;
                capturePeriod = (int) frames;
            }
            else
            {
                logLine ("capture: exclusive refused (0x%08lx), using the shared stream", (unsigned long) hr);
                releaseCom (captureClient);
            }
        }
        if (! captureExclusive && SUCCEEDED (d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &captureClient)))
        {
            captureFormat = makeFormat (capture.channels, (int) sampleRate, SampleKind::float32, capture.channelMask);
            capturePeriod = initShared (captureClient, captureFormat, sampleRate, capture.mixRate, 1000000); // 100 ms of slack
            if (capturePeriod == 0)
                releaseCom (captureClient);
        }
        if (captureClient != nullptr)
        {
            captureEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
            if (FAILED (captureClient->SetEventHandle (captureEvent))
                || FAILED (captureClient->GetService (__uuidof (IAudioCaptureClient), (void**) &captureService)))
            {
                releaseCom (captureService);
                releaseCom (captureClient);
                CloseHandle (captureEvent);
                captureEvent = nullptr;
            }
        }
        if (captureClient != nullptr)
        {
            REFERENCE_TIME lat = 0;
            captureClient->GetStreamLatency (&lat);
            captureLatency = refTimeToFrames (lat, sampleRate);
            inputTarget = inputTargetFor (capturePeriod, devicePeriod, blockSize);
            logLine ("capture: %s mode, %s %d ch @ %.0f Hz, period %d, stream latency %d, FIFO target %d",
                     captureExclusive ? "exclusive" : "shared", kindName (captureFormat.kind), captureFormat.channels, sampleRate,
                     capturePeriod, captureLatency, inputTarget);
        }
        else
            logLine ("capture: could not open \"%s\"", narrow (capture.name).c_str());
        d->Release();
    }

    void closeDevices()
    {
        if (renderClient != nullptr)
            renderClient->Stop();
        if (captureClient != nullptr)
            captureClient->Stop();
        releaseCom (renderService);
        releaseCom (renderClient);
        releaseCom (captureService);
        releaseCom (captureClient);
        for (auto* h : { &renderEvent, &captureEvent })
            if (*h != nullptr)
            {
                CloseHandle (*h);
                *h = nullptr;
            }
    }

    // Moves every captured packet into the input FIFO (stamped with the time it was taken).
    void drainCapture()
    {
        if (captureService == nullptr)
            return;
        packetsThisDrain = 0;
        for (;;)
        {
            UINT32 packet = 0;
            const HRESULT hr = captureService->GetNextPacketSize (&packet);
            if (FAILED (hr))
            {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
                    deviceLost = true;
                break;
            }
            if (packet == 0)
                break;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED (captureService->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
                break;
            scratch.resize ((size_t) frames * (size_t) captureFormat.channels);
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                std::fill (scratch.begin(), scratch.end(), 0.0f);
            else
                readSamples (data, scratch.data(), (int) frames * captureFormat.channels, captureFormat.kind);
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0 && now() - startMs > 200.0)
            {
                ++captureGaps; // we were too late and the device dropped frames
                if (captureGaps <= 8)
                    gapNotes[captureGaps - 1] = { (float) (now() - startMs), (float) (now() - lastDrainMs), (int) frames, (int) packetsThisDrain };
            }
            ++packetsThisDrain;
            if (traced < 240 && now() - startMs >= 1000.0 * (traced + 1))
                trace[traced++] = { (float) inFifo.averageFill(), (float) ((inFifo.ratio() - 1.0) * 1.0e6), (int) inFifo.underflows, (int) captureGaps };
            inFifo.push (scratch.data(), (int) frames, now() / 1000.0);
            captureService->ReleaseBuffer (frames);
        }
        if (packetsThisDrain > 0)
        {
            const double t = now();
            if (lastDrainMs > 0.0 && t - startMs > 500.0)
                maxCaptureGapMs = std::max (maxCaptureGapMs, t - lastDrainMs);
            lastDrainMs = t;
        }
    }

    static double now() { return nowMs(); }

    // One host block: input FIFO -> ASIO input buffers, bufferSwitch, ASIO output buffers -> output FIFO.
    void runHostBlock()
    {
        const int b = blockSize;
        if (captureService != nullptr)
        {
            if (! inputSettled && now() - startMs > 300.0)
            {
                inputSettled = true;
                inFifo.recenter (now() / 1000.0);
            }
            const int raises = inFifo.raises();
            inFifo.pull (blockIn.data(), b, now() / 1000.0);
            if (inFifo.raises() != raises && hostKnowsLatencyChanges) // the input latency grew: hosts that can re-read it
                callbacks->asioMessage (kAsioLatenciesChanged, 0, nullptr, nullptr);
            const int stride = inFifo.numChannels();
            for (auto& c : channels)
                if (c.isInput)
                {
                    float* dst = c.buffers[bufferIndex];
                    for (int i = 0; i < b; ++i)
                        dst[i] = blockIn[(size_t) (i * stride + c.index)];
                }
        }
        else
            for (auto& c : channels)
                if (c.isInput)
                    std::fill (c.buffers[bufferIndex], c.buffers[bufferIndex] + b, 0.0f);

        lastPosition = position;
        lastSystemNs = (long long) (nowMs() * 1.0e6);
        if (useTimeInfo)
        {
            ASIOTime t {};
            t.timeInfo.speed = 1.0;
            t.timeInfo.systemTime.hi = (unsigned long) ((unsigned long long) lastSystemNs.load() >> 32);
            t.timeInfo.systemTime.lo = (unsigned long) ((unsigned long long) lastSystemNs.load() & 0xffffffffu);
            t.timeInfo.samplePosition.hi = (unsigned long) ((unsigned long long) position >> 32);
            t.timeInfo.samplePosition.lo = (unsigned long) ((unsigned long long) position & 0xffffffffu);
            t.timeInfo.sampleRate = sampleRate;
            t.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;
            callbacks->bufferSwitchTimeInfo (&t, bufferIndex, ASIOTrue);
        }
        else
            callbacks->bufferSwitch (bufferIndex, ASIOTrue);

        const int stride = renderFormat.channels;
        std::fill (blockOut.begin(), blockOut.end(), 0.0f);
        for (auto& c : channels)
            if (! c.isInput)
            {
                const float* src = c.buffers[bufferIndex];
                for (int i = 0; i < b; ++i)
                    blockOut[(size_t) (i * stride + c.index)] = src[i];
            }
        outFifo.push (blockOut.data(), b);
        position += b;
        bufferIndex ^= 1;
        ++hostBlocks;
    }

    // Writes `frames` frames from the output FIFO (host blocks run until it holds enough) into the device.
    void feedDevice (int frames)
    {
        if (frames <= 0)
            return;
        const double t0 = nowMs();
        while (outFifo.count < frames)
        {
            runHostBlock();
            drainCapture(); // a long burst must not let the recording buffer overflow
        }
        const double used = nowMs() - t0;
        maxBurstMs = std::max (maxBurstMs, used);
        if (used > 1000.0 * devicePeriod / sampleRate)
            ++slowBursts;
        BYTE* data = nullptr;
        if (FAILED (renderService->GetBuffer ((UINT32) frames, &data)))
            return;
        scratch.resize ((size_t) (frames * renderFormat.channels));
        outFifo.pop (scratch.data(), frames);
        writeSamples (scratch.data(), data, frames * renderFormat.channels, renderFormat.kind);
        renderService->ReleaseBuffer ((UINT32) frames, 0);
    }

    void prime()
    {
        const int ch = renderFormat.channels;
        outFifo.reset (ch, (int) renderFrames + 2 * blockSize + 64);
        if (captureClient != nullptr) // starts at its target level (silence), then follows the capture clock
            inFifo.reset (captureFormat.channels, inputTarget, sampleRate);
        blockIn.assign ((size_t) (blockSize * std::max (1, captureFormat.channels)), 0.0f);
        blockOut.assign ((size_t) (blockSize * ch), 0.0f);
        // the first buffer: silence, so the stream starts without a gap
        BYTE* data = nullptr;
        const int first = streamExclusive ? (int) renderFrames : deviceQueue;
        if (SUCCEEDED (renderService->GetBuffer ((UINT32) first, &data)))
            renderService->ReleaseBuffer ((UINT32) first, AUDCLNT_BUFFERFLAGS_SILENT);
        bufferIndex = 0;
        events = hostBlocks = lateEvents = inputShortBefore = slowBursts = captureEvents = captureGaps = 0;
        traced = 0;
        inputSettled = false;
        lastDrainMs = maxCaptureGapMs = 0.0;
        lastEventMs = maxGapMs = maxBurstMs = 0.0;
        startMs = nowMs();
    }

    void onDeviceEvent()
    {
        const double t = nowMs();
        if (lastEventMs > 0.0 && t - startMs > 200.0) // the device's first periods are irregular: not counted
        {
            const double gap = t - lastEventMs;
            maxGapMs = std::max (maxGapMs, gap);
            if (gap > 1.5 * 1000.0 * devicePeriod / sampleRate + 1.0)
                ++lateEvents;
        }
        lastEventMs = t;
        ++events;
        if (t - startMs < 200.0)
            inputShortBefore = inFifo.underflows;
        drainCapture();
        if (streamExclusive)
            feedDevice ((int) renderFrames);
        else
        {
            UINT32 padding = 0;
            if (FAILED (renderClient->GetCurrentPadding (&padding)))
            {
                deviceLost = true;
                return;
            }
            feedDevice (deviceQueue - (int) padding);
        }
    }

    void logStats()
    {
        logLine ("stats: %lld device events, %lld host blocks, late events %lld (max gap %.2f ms), slow bursts %lld (max %.2f ms)",
                 events, hostBlocks, lateEvents, maxGapMs, slowBursts, maxBurstMs);
        if (captureClient != nullptr)
            logLine ("input: %lld capture events, FIFO level %.0f (target %d, now %d), clock ratio %+.1f ppm (range %+.1f..%+.1f), "
                     "underflows %lld (+%lld while starting), overflows %lld, device gaps %lld, longest wait for a packet %.2f ms",
                     captureEvents, inFifo.averageFill(), inputTarget, inFifo.target(), (inFifo.ratio() - 1.0) * 1.0e6, (inFifo.minRatio - 1.0) * 1.0e6,
                     (inFifo.maxRatio - 1.0) * 1.0e6, (long long) inFifo.underflows - inputShortBefore, inputShortBefore,
                     (long long) inFifo.overflows, captureGaps, maxCaptureGapMs);
        if (captureClient != nullptr && traced > 0)
        {
            std::string line = "input per second (level/ppm/underflows/gaps):";
            for (int i = 0; i < traced; ++i)
            {
                char t[64];
                std::snprintf (t, sizeof (t), " %.0f/%+.0f/%d/%d", trace[i].level, trace[i].ppm, trace[i].underflows, trace[i].gaps);
                line += t;
            }
            logLine ("%s", line.substr (0, 1000).c_str());
        }
        for (int i = 0; i < (int) std::min<long long> (8, captureGaps); ++i)
            logLine ("gap %d at %.1f ms: %.2f ms since the last packet, %d frames, packet %d of its drain", i, gapNotes[i].atMs,
                     gapNotes[i].sinceDrainMs, gapNotes[i].frames, gapNotes[i].packetIndex);
    }

    static DWORD WINAPI audioThreadMain (LPVOID self)
    {
        ((LynxAsio*) self)->audioThread();
        return 0;
    }

    void audioThread()
    {
        const auto com = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        DWORD taskIndex = 0;
        HANDLE task = nullptr;

        openOk = false;
        if (IMMDevice* d = openEndpoint (eRender, settings.output))
        {
            openOk = openRender (d);
            d->Release();
        }
        else
            setError ("The playback device is not available.");
        if (openOk)
            openCapture();
        SetEvent (doneEvent);

        bool streaming = false;
        for (;;)
        {
            // the capture event comes first: when both are signalled, the recorded packet is taken before the block
            HANDLE waits[3] = { wakeEvent, captureEvent != nullptr ? captureEvent : renderEvent, renderEvent };
            const DWORD n = ! streaming ? 1 : captureEvent != nullptr ? 3 : 2;
            const DWORD r = WaitForMultipleObjects (n, waits, FALSE, streaming ? 2000 : INFINITE);
            if (r == WAIT_OBJECT_0)
            {
                const auto c = pending.exchange (Command::none);
                if (c == Command::start && openOk && ! streaming)
                {
                    task = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
                    if (task != nullptr)
                        AvSetMmThreadPriority (task, AVRT_PRIORITY_CRITICAL);
                    prime();
                    if (captureClient != nullptr)
                        captureClient->Start();
                    streaming = SUCCEEDED (renderClient->Start());
                    commandOk = streaming;
                }
                else if (c == Command::stop || c == Command::quit)
                {
                    if (streaming)
                    {
                        renderClient->Stop();
                        renderClient->Reset(); // a later start begins with an empty queue
                        if (captureClient != nullptr)
                        {
                            captureClient->Stop();
                            captureClient->Reset();
                        }
                        logStats();
                        if (task != nullptr)
                            AvRevertMmThreadCharacteristics (task);
                        task = nullptr;
                    }
                    streaming = false;
                    commandOk = true;
                    if (c == Command::quit)
                    {
                        closeDevices();
                        SetEvent (doneEvent);
                        break;
                    }
                }
                else
                    commandOk = c != Command::start || streaming;
                SetEvent (doneEvent);
            }
            else if (r == WAIT_OBJECT_0 + 1 && n == 3)
            {
                ++captureEvents;
                drainCapture();
            }
            else if (r == WAIT_OBJECT_0 + 1 || r == WAIT_OBJECT_0 + 2)
                onDeviceEvent();
            else if (r == WAIT_TIMEOUT && streaming)
            {
                ++lateEvents; // the device stopped calling (unplugged, sleep): ask the host to reopen
                if (callbacks != nullptr)
                    callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr);
            }
            if (deviceLost)
            {
                deviceLost = false;
                if (callbacks != nullptr)
                    callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr);
            }
        }
        if (SUCCEEDED (com))
            CoUninitialize();
    }

    std::atomic<long> refs { 1 };
    HWND owner = nullptr;
    char error[128] = "";
    bool initialised = false;
    double sampleRate = 48000.0;
    ASIOCallbacks* callbacks = nullptr;
    bool useTimeInfo = false, hostKnowsLatencyChanges = false;
    std::vector<Channel> channels;
    std::vector<float> memory;
    int blockSize = 0;
    int deviceQueue = 0;
    int capturePeriod = 0, inputTarget = 0, renderLatency = 0, captureLatency = 0;
    bool captureExclusive = false;
    HANDLE thread = nullptr, wakeEvent = nullptr, doneEvent = nullptr;
    std::atomic<Command> pending { Command::none };
    std::atomic<bool> commandOk { false };
    bool openOk = false, running = false, deviceLost = false, streamExclusive = false;
    std::atomic<long long> lastPosition { 0 }, lastSystemNs { 0 };
};

//==============================================================================================================
// Control panel: a small window built in code (mode, devices, buffer, safety margin)

enum
{
    idMode = 101,
    idOutput,
    idInput,
    idBuffer,
    idSafety,
    idInfo,
    idLatency,
    idOk,
    idCancel
};

struct Panel
{
    LynxAsio& driver;
    Settings edit;
    std::vector<Endpoint> outputs, inputs;
    HWND window = nullptr;
    bool done = false, accepted = false;
    HFONT font = nullptr;

    HWND item (int id) const { return GetDlgItem (window, id); }

    static void addString (HWND combo, const std::wstring& s) { SendMessageW (combo, CB_ADDSTRING, 0, (LPARAM) s.c_str()); }

    void fillBuffers()
    {
        HWND b = item (idBuffer);
        SendMessageW (b, CB_RESETCONTENT, 0, 0);
        addString (b, tr (L"Automatic", L"Automático"));
        const double rate = driver.render.mixRate > 0 ? driver.render.mixRate : 48000.0;
        const bool exclusive = edit.mode == Mode::exclusive && driver.render.exclusiveMinPeriod > 0;
        std::vector<int> sizes;
        if (exclusive)
            sizes.push_back (driver.render.exclusiveMinPeriod);
        for (int s : { 32, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048 })
            if (! exclusive || s > driver.render.exclusiveMinPeriod)
                sizes.push_back (s);
        int selected = 0;
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            wchar_t text[64];
            swprintf (text, 64, tr (L"%d samples (%.1f ms)", L"%d amostras (%.1f ms)"), sizes[i], 1000.0 * sizes[i] / rate);
            addString (b, text);
            SendMessageW (b, CB_SETITEMDATA, i + 1, (LPARAM) sizes[i]);
            if (sizes[i] == edit.bufferSize)
                selected = (int) i + 1;
        }
        SendMessageW (b, CB_SETCURSEL, (WPARAM) selected, 0);
    }

    void updateInfo()
    {
        const auto& r = driver.render;
        const double rate = r.mixRate > 0 ? r.mixRate : 48000.0;
        wchar_t text[600];
        if (! r.ok)
            swprintf (text, 600, L"%ls", tr (L"No playback device is available.", L"Nenhum dispositivo de reprodução disponível."));
        else if (edit.mode == Mode::exclusive)
        {
            if (r.exclusiveMinPeriod > 0)
                swprintf (text, 600, tr (L"Exclusive: LYNX ASIO uses \"%ls\" alone (other programs go silent; the input device too, when it accepts exclusive mode). Shortest period on this device: %d samples (%.1f ms).",
                                         L"Exclusivo: o LYNX ASIO usa \"%ls\" sozinho (os outros programas ficam mudos; o dispositivo de entrada também, quando aceita o modo exclusivo). Menor período neste dispositivo: %d amostras (%.1f ms)."),
                          r.name.c_str(), r.exclusiveMinPeriod, 1000.0 * r.exclusiveMinPeriod / rate);
            else
                swprintf (text, 600, L"%ls", tr (L"This device does not accept exclusive mode; the shared mode will be used.",
                                                 L"Este dispositivo não aceita o modo exclusivo; o modo compartilhado será usado."));
        }
        else
            swprintf (text, 600, tr (L"Shared: other programs keep their sound. Windows mixes \"%ls\" every %d samples (%.1f ms)%ls; the latency cannot go below that.",
                                     L"Compartilhado: os outros programas continuam com som. O Windows mistura \"%ls\" a cada %d amostras (%.1f ms)%ls; a latência não fica menor que isso."),
                      r.name.c_str(), r.sharedPeriod, 1000.0 * r.sharedPeriod / rate,
                      r.sharedPeriod < r.classicPeriod ? tr (L" (low-latency period)", L" (período de baixa latência)") : L"");
        SetWindowTextW (item (idInfo), text);
        updateLatency();
    }

    // "Latency" readout: what these settings give, and what the open stream really uses.
    void updateLatency()
    {
        const auto& r = driver.render;
        const double rate = r.mixRate > 0 ? r.mixRate : 48000.0;
        auto ms = [rate] (int frames) { return 1000.0 * frames / rate; };
        std::wstring text;
        wchar_t line[400];
        if (r.ok)
        {
            const auto l = driver.latencies (driver.preferredBlock(), false);
            swprintf (line, 400, tr (L"Latency at %d samples (%.1f ms): output %.1f ms", L"Latência com %d amostras (%.1f ms): saída %.1f ms"),
                      l.block, ms (l.block), ms (l.output));
            text = line;
            if (l.hasInput)
            {
                swprintf (line, 400, tr (L", input %.1f ms (%ls)", L", entrada %.1f ms (%ls)"), ms (l.input),
                          l.captureExclusive ? tr (L"exclusive", L"exclusiva") : tr (L"shared", L"compartilhada"));
                text += line;
            }
            else
                text += tr (L", no input", L", sem entrada");
            text += L".";
            const auto now = driver.latencies (0, true);
            if (now.open)
            {
                swprintf (line, 400, tr (L"\nRunning now: %ls, buffer %d, output %.1f ms", L"\nEm uso agora: %ls, buffer %d, saída %.1f ms"),
                          now.exclusive ? tr (L"exclusive", L"exclusivo") : tr (L"shared", L"compartilhado"), now.block, ms (now.output));
                text += line;
                if (now.hasInput)
                {
                    swprintf (line, 400, tr (L", input %.1f ms (%ls)", L", entrada %.1f ms (%ls)"), ms (now.input),
                              now.captureExclusive ? tr (L"exclusive", L"exclusiva") : tr (L"shared", L"compartilhada"));
                    text += line;
                }
                text += tr (L". Changes apply when the program restarts the driver.", L". As mudanças valem quando o programa reiniciar o driver.");
            }
        }
        SetWindowTextW (item (idLatency), text.c_str());
    }

    void readControls()
    {
        edit.mode = SendMessageW (item (idMode), CB_GETCURSEL, 0, 0) == 1 ? Mode::exclusive : Mode::shared;
        const auto o = (int) SendMessageW (item (idOutput), CB_GETCURSEL, 0, 0);
        edit.output = o <= 0 || o > (int) outputs.size() ? std::wstring() : outputs[(size_t) o - 1].id;
        const auto in = (int) SendMessageW (item (idInput), CB_GETCURSEL, 0, 0);
        edit.input = in <= 0 ? std::wstring() : in == 1 ? std::wstring (L"-") : in - 2 < (int) inputs.size() ? inputs[(size_t) in - 2].id : std::wstring();
        const auto b = (int) SendMessageW (item (idBuffer), CB_GETCURSEL, 0, 0);
        edit.bufferSize = b <= 0 ? 0 : (int) SendMessageW (item (idBuffer), CB_GETITEMDATA, (WPARAM) b, 0);
        edit.safety = std::clamp ((int) SendMessageW (item (idSafety), CB_GETCURSEL, 0, 0), 0, 2);
    }

    void create (HWND owner)
    {
        NONCLIENTMETRICSW m {};
        m.cbSize = sizeof (m);
        SystemParametersInfoW (SPI_GETNONCLIENTMETRICS, sizeof (m), &m, 0);
        font = CreateFontIndirectW (&m.lfMessageFont);
        HDC screen = GetDC (nullptr);
        const int dpi = GetDeviceCaps (screen, LOGPIXELSY);
        ReleaseDC (nullptr, screen);
        auto px = [dpi] (int v) { return MulDiv (v, dpi > 0 ? dpi : 96, 96); };

        RECT rc { 0, 0, px (460), px (392) };
        AdjustWindowRectEx (&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
        window = CreateWindowExW (WS_EX_DLGMODALFRAME, L"LynxAsioPanel", L"LYNX ASIO", WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, owner, nullptr, moduleHandle, this);
        auto make = [&] (const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id)
        {
            HWND c = CreateWindowExW (0, cls, text, WS_CHILD | WS_VISIBLE | style, px (x), px (y), px (w), px (h), window, (HMENU) (INT_PTR) id, moduleHandle, nullptr);
            SendMessageW (c, WM_SETFONT, (WPARAM) font, TRUE);
            return c;
        };
        const DWORD combo = CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL;
        int y = 14;
        auto row = [&] (const wchar_t* label, int id)
        {
            make (L"STATIC", label, SS_LEFT, 14, y + 3, 130, 20, 0);
            make (L"COMBOBOX", L"", combo, 150, y, 296, 300, id);
            y += 34;
        };
        row (tr (L"Mode", L"Modo"), idMode);
        row (tr (L"Output", L"Saída"), idOutput);
        row (tr (L"Input", L"Entrada"), idInput);
        row (tr (L"Buffer size", L"Tamanho do buffer"), idBuffer);
        row (tr (L"Safety margin", L"Margem de segurança"), idSafety);
        make (L"STATIC", L"", SS_LEFT, 14, y + 4, 432, 66, idInfo);
        make (L"STATIC", L"", SS_LEFT, 14, y + 72, 432, 56, idLatency);
        make (L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 270, 352, 84, 28, idOk);
        make (L"BUTTON", tr (L"Cancel", L"Cancelar"), BS_PUSHBUTTON | WS_TABSTOP, 362, 352, 84, 28, idCancel);

        addString (item (idMode), tr (L"Shared (other programs keep their sound)", L"Compartilhado (os outros programas continuam com som)"));
        addString (item (idMode), tr (L"Exclusive (lowest latency, device used alone)", L"Exclusivo (menor latência, dispositivo só do LYNX ASIO)"));
        SendMessageW (item (idMode), CB_SETCURSEL, edit.mode == Mode::exclusive ? 1 : 0, 0);

        auto lists = [this]
        {
            outputs = listEndpoints (eRender);
            inputs = listEndpoints (eCapture);
        };
        onMtaThread (lists);
        addString (item (idOutput), tr (L"Windows default device", L"Dispositivo padrão do Windows"));
        int sel = 0;
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            addString (item (idOutput), outputs[i].name);
            if (outputs[i].id == edit.output)
                sel = (int) i + 1;
        }
        SendMessageW (item (idOutput), CB_SETCURSEL, (WPARAM) sel, 0);
        addString (item (idInput), tr (L"Windows default device", L"Dispositivo padrão do Windows"));
        addString (item (idInput), tr (L"None", L"Nenhuma"));
        sel = edit.input == L"-" ? 1 : 0;
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            addString (item (idInput), inputs[i].name);
            if (inputs[i].id == edit.input)
                sel = (int) i + 2;
        }
        SendMessageW (item (idInput), CB_SETCURSEL, (WPARAM) sel, 0);
        addString (item (idSafety), tr (L"None (lowest latency, may click)", L"Nenhuma (menor latência, pode estalar)"));
        addString (item (idSafety), tr (L"Small", L"Pequena"));
        addString (item (idSafety), tr (L"Normal", L"Normal"));
        SendMessageW (item (idSafety), CB_SETCURSEL, (WPARAM) edit.safety, 0);
        fillBuffers();
        updateInfo();

        RECT wr {}, or_ {};
        GetWindowRect (window, &wr);
        if (owner != nullptr && GetWindowRect (owner, &or_))
            SetWindowPos (window, nullptr, (or_.left + or_.right - (wr.right - wr.left)) / 2, (or_.top + or_.bottom - (wr.bottom - wr.top)) / 2, 0, 0,
                          SWP_NOSIZE | SWP_NOZORDER);
        ShowWindow (window, SW_SHOW);
    }

    static LRESULT CALLBACK proc (HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
            SetWindowLongPtrW (h, GWLP_USERDATA, (LONG_PTR) ((CREATESTRUCTW*) lp)->lpCreateParams);
        auto* p = (Panel*) GetWindowLongPtrW (h, GWLP_USERDATA);
        if (p != nullptr)
        {
            switch (msg)
            {
                case WM_COMMAND:
                {
                    const int id = LOWORD (wp);
                    if (id == idOk || id == IDOK)
                    {
                        p->readControls();
                        p->accepted = true;
                        p->done = true;
                        return 0;
                    }
                    if (id == idCancel || id == IDCANCEL)
                    {
                        p->done = true;
                        return 0;
                    }
                    if (HIWORD (wp) == CBN_SELCHANGE && (id == idMode || id == idOutput || id == idInput))
                    {
                        p->readControls();
                        // re-probe the chosen device so the sizes, the explanation and the latency match it
                        p->driver.settings = p->edit;
                        p->driver.probe();
                        p->fillBuffers();
                        p->readControls();
                        p->driver.settings = p->edit;
                        p->updateInfo();
                    }
                    else if (HIWORD (wp) == CBN_SELCHANGE && (id == idBuffer || id == idSafety))
                    {
                        p->readControls();
                        p->driver.settings = p->edit; // restored by showPanel on Cancel
                        p->updateLatency();
                    }
                    break;
                }
                case WM_CLOSE:
                    p->done = true;
                    return 0;
                default:
                    break;
            }
        }
        return DefWindowProcW (h, msg, wp, lp);
    }
};

INT_PTR showPanel (LynxAsio& driver, HWND owner)
{
    static bool registered = false;
    if (! registered)
    {
        WNDCLASSW wc {};
        wc.lpfnWndProc = Panel::proc;
        wc.hInstance = moduleHandle;
        wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LynxAsioPanel";
        registered = RegisterClassW (&wc) != 0;
    }
    const auto original = driver.settings;
    Panel panel { driver, driver.settings, {}, {} };
    panel.create (owner);
    if (owner != nullptr)
        EnableWindow (owner, FALSE);
    MSG msg;
    while (! panel.done && GetMessageW (&msg, nullptr, 0, 0) > 0)
    {
        if (! IsDialogMessageW (panel.window, &msg))
        {
            TranslateMessage (&msg);
            DispatchMessageW (&msg);
        }
    }
    if (owner != nullptr)
    {
        EnableWindow (owner, TRUE);
        SetForegroundWindow (owner);
    }
    DestroyWindow (panel.window);
    if (panel.font != nullptr)
        DeleteObject (panel.font);
    driver.settings = panel.accepted ? panel.edit : original;
    if (! panel.accepted)
        driver.probe();
    return panel.accepted ? IDOK : IDCANCEL;
}

//==============================================================================================================
// COM class factory and registration

class Factory final : public IClassFactory
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, void** out) override
    {
        if (IsEqualIID (iid, IID_IUnknown) || IsEqualIID (iid, IID_IClassFactory))
        {
            *out = static_cast<IClassFactory*> (this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance (IUnknown* outer, REFIID iid, void** out) override
    {
        if (outer != nullptr)
            return CLASS_E_NOAGGREGATION;
        auto* d = new LynxAsio();
        const auto hr = d->QueryInterface (iid, out);
        d->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer (BOOL lock) override
    {
        lock ? ++serverLocks : --serverLocks;
        return S_OK;
    }
};

Factory factory;

std::wstring clsidString()
{
    LPOLESTR s = nullptr;
    StringFromCLSID (CLSID_LynxAsio, &s);
    std::wstring r = s != nullptr ? s : L"";
    CoTaskMemFree (s);
    return r;
}

LONG setValue (HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value)
{
    HKEY k = nullptr;
    auto r = RegCreateKeyExW (root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (r == ERROR_SUCCESS)
    {
        r = RegSetValueExW (k, name, 0, REG_SZ, (const BYTE*) value.c_str(), (DWORD) ((value.size() + 1) * sizeof (wchar_t)));
        RegCloseKey (k);
    }
    return r;
}
} // namespace

extern "C" BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        moduleHandle = instance;
        DisableThreadLibraryCalls (instance);
    }
    return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject (REFCLSID clsid, REFIID iid, void** out)
{
    if (! IsEqualCLSID (clsid, CLSID_LynxAsio))
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory.QueryInterface (iid, out);
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    return serverLocks == 0 && liveObjects == 0 ? S_OK : S_FALSE;
}

// regsvr32 lynx-asio.dll (as administrator): the COM class and the entry in the ASIO driver list.
extern "C" HRESULT STDAPICALLTYPE DllRegisterServer()
{
    wchar_t path[MAX_PATH * 2] {};
    GetModuleFileNameW (moduleHandle, path, MAX_PATH * 2);
    const auto clsid = clsidString();
    const auto classKey = L"SOFTWARE\\Classes\\CLSID\\" + clsid;
    LONG r = setValue (HKEY_LOCAL_MACHINE, classKey, nullptr, driverNameW);
    if (r == ERROR_SUCCESS)
        r = setValue (HKEY_LOCAL_MACHINE, classKey + L"\\InprocServer32", nullptr, path);
    if (r == ERROR_SUCCESS)
        r = setValue (HKEY_LOCAL_MACHINE, classKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    const std::wstring asioKey = std::wstring (L"SOFTWARE\\ASIO\\") + driverNameW;
    if (r == ERROR_SUCCESS)
        r = setValue (HKEY_LOCAL_MACHINE, asioKey, L"CLSID", clsid);
    if (r == ERROR_SUCCESS)
        r = setValue (HKEY_LOCAL_MACHINE, asioKey, L"Description", driverNameW);
    return r == ERROR_SUCCESS ? S_OK : (r == ERROR_ACCESS_DENIED ? E_ACCESSDENIED : E_FAIL);
}

extern "C" HRESULT STDAPICALLTYPE DllUnregisterServer()
{
    const auto clsid = clsidString();
    RegDeleteTreeW (HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + clsid).c_str());
    const auto r = RegDeleteTreeW (HKEY_LOCAL_MACHINE, (std::wstring (L"SOFTWARE\\ASIO\\") + driverNameW).c_str());
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND ? S_OK : E_ACCESSDENIED;
}
