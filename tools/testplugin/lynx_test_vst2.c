/* A minimal VST2 instrument for testing LYNX's own VST2 host (src/engine/Vst2Format.cpp) without third-party
 * plugins. Written against the VST 2.4 binary interface as LYNX declares it (no Steinberg headers):
 *   zig cc -shared -O2 -target x86_64-windows-gnu tools/testplugin/lynx_test_vst2.c -o "LYNX Test Synth.dll"
 *   LYNX.exe --vst2test "LYNX Test Synth.dll" report.txt
 * Sound: a sine at the played note, amplitude = parameter 0 ("Gain") × velocity.
 * Parameter 1 ("Tempo") reports the host tempo / 1000 it last read through audioMasterGetTime.
 * State: a program chunk ('LXV2' + gain + last tempo/1000), so a host must restore it with effSetChunk.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct Effect Effect;
typedef intptr_t (*HostCallback) (Effect*, int32_t, int32_t, intptr_t, void*, float);

struct Effect
{
    int32_t magic;
    intptr_t (*dispatcher) (Effect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process) (Effect*, float**, float**, int32_t);
    void (*setParameter) (Effect*, int32_t, float);
    float (*getParameter) (Effect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueId, version;
    void (*processReplacing) (Effect*, float**, float**, int32_t);
    void* processDoubleReplacing;
    char future[56];
};

typedef struct
{
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char data[4];
    char detune, noteOffVelocity, r1, r2;
} MidiEvent;

typedef struct
{
    int32_t numEvents;
    intptr_t reserved;
    MidiEvent* events[2];
} Events;

typedef struct
{
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t num, den, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} TimeInfo;

typedef struct
{
    Effect effect;
    HostCallback host;
    float gain, tempo, rate;
    int note;
    float amplitude;
    double phase;
    unsigned char chunk[12];
} Synth;

static intptr_t dispatch (Effect* e, int32_t op, int32_t index, intptr_t value, void* ptr, float opt)
{
    Synth* s = (Synth*) e->object;
    (void) index;
    switch (op)
    {
        case 1: free (s); return 0;                                 /* close */
        case 10: s->rate = opt; return 0;                           /* setSampleRate */
        case 8: strcpy ((char*) ptr, index == 0 ? "Gain" : "Tempo"); return 0; /* getParamName */
        case 23:                                                    /* getChunk */
            memcpy (s->chunk, "LXV2", 4);
            memcpy (s->chunk + 4, &s->gain, 4);
            memcpy (s->chunk + 8, &s->tempo, 4); /* what the host's time info said (for the host test) */
            *(void**) ptr = s->chunk;
            return 12;
        case 24:                                                    /* setChunk */
            if (value >= 8 && memcmp (ptr, "LXV2", 4) == 0)
                memcpy (&s->gain, (char*) ptr + 4, 4);
            return 1;
        case 25:                                                    /* processEvents */
        {
            Events* ev = (Events*) ptr;
            for (int i = 0; i < ev->numEvents; ++i)
            {
                const unsigned char* d = ev->events[i]->data;
                if ((d[0] & 0xF0) == 0x90 && d[2] > 0) { s->note = d[1]; s->amplitude = d[2] / 127.0f; }
                else if ((d[0] & 0xF0) == 0x80 || ((d[0] & 0xF0) == 0x90 && d[2] == 0)) { if (d[1] == s->note) s->amplitude = 0.0f; }
            }
            return 1;
        }
        case 35: return 2;                                          /* getPlugCategory: synth */
        case 45: strcpy ((char*) ptr, "LYNX Test Synth"); return 1; /* getEffectName */
        case 47: strcpy ((char*) ptr, "LYNX"); return 1;            /* getVendorString */
        case 58: return 2400;                                       /* getVstVersion */
        default: return 0;
    }
}

static void setParam (Effect* e, int32_t i, float v)
{
    Synth* s = (Synth*) e->object;
    if (i == 0) s->gain = v;
}

static float getParam (Effect* e, int32_t i)
{
    Synth* s = (Synth*) e->object;
    return i == 0 ? s->gain : s->tempo;
}

static void processReplacing (Effect* e, float** in, float** out, int32_t n)
{
    Synth* s = (Synth*) e->object;
    (void) in;
    const TimeInfo* t = (const TimeInfo*) s->host (e, 7, 0, 0, 0, 0.0f);
    if (t != 0 && (t->flags & (1 << 10)))
        s->tempo = (float) (t->tempo / 1000.0);
    const double step = 440.0 * pow (2.0, (s->note - 69) / 12.0) / (s->rate > 0 ? s->rate : 44100.0);
    for (int32_t i = 0; i < n; ++i)
    {
        const float v = s->gain * s->amplitude * (float) sin (2.0 * 3.14159265358979 * s->phase);
        s->phase += step;
        if (s->phase >= 1.0) s->phase -= 1.0;
        out[0][i] = v;
        out[1][i] = v;
    }
}

__declspec (dllexport) Effect* VSTPluginMain (HostCallback host)
{
    Synth* s = (Synth*) calloc (1, sizeof (Synth));
    if (s == 0 || host (0, 1, 0, 0, 0, 0.0f) == 0) /* audioMasterVersion */
        return 0;
    s->host = host;
    s->gain = 0.5f;
    s->rate = 44100.0f;
    s->note = 69;
    s->effect.magic = 0x56737450;
    s->effect.dispatcher = dispatch;
    s->effect.setParameter = setParam;
    s->effect.getParameter = getParam;
    s->effect.processReplacing = processReplacing;
    s->effect.numPrograms = 1;
    s->effect.numParams = 2;
    s->effect.numInputs = 0;
    s->effect.numOutputs = 2;
    s->effect.flags = (1 << 4) | (1 << 5) | (1 << 8); /* replacing, program chunks, synth */
    s->effect.object = s;
    s->effect.uniqueId = 0x4C585632;                    /* 'LXV2' */
    s->effect.version = 1;
    return &s->effect;
}
