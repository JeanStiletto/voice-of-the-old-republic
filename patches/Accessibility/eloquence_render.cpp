#include "eloquence_render.h"

#include <windows.h>
#include <deque>
#include <string>
#include <vector>

#include "log.h"
#include "nvda_config.h"

namespace acc::eloquence {

namespace {

// ---- ECI ABI (32-bit, __stdcall, undecorated exports) ----------------------
// Constants as the IBMTTS driver declares them (_ibmeci.py).
constexpr int kParamSynthMode   = 0;
constexpr int kParamInputType   = 1;
constexpr int kParamSampleRate  = 5;
constexpr int kVoiceHeadSize    = 1;
constexpr int kVoicePitch       = 2;   // eciPitchBaseline
constexpr int kVoiceInflection  = 3;   // eciPitchFluctuation
constexpr int kVoiceRoughness   = 4;
constexpr int kVoiceBreathiness = 5;
constexpr int kVoiceSpeed       = 6;
constexpr int kVoiceVolume      = 7;
constexpr int kMsgWaveform      = 0;   // eciWaveformBuffer: lParam = samples ready
constexpr int kMsgIndexReply    = 2;   // eciIndexReply:     lParam = index value
constexpr int kDataProcessed    = 1;
constexpr int kFullVolume       = 100; // eciVolume range is 0..100
constexpr int kEndMark          = 0xffff;  // index inserted after the text

// NVDA rate percent → ECI speed, as the driver's _percentToParam(40..156).
constexpr int    kMinRate         = 40;
constexpr int    kMaxRate         = 156;
constexpr double kRateBoostFactor = 1.6;

constexpr int   kBufferSamples   = 3300;   // the driver's output-buffer size
constexpr DWORD kRenderTimeoutMs = 5000;

typedef int   (__stdcall* PFN_EciCallback)(void* eci, int msg, long lParam, void* data);
typedef void* (__stdcall* PFN_eciNewEx)(int dialect);
typedef void* (__stdcall* PFN_eciDelete)(void* eci);
typedef void  (__stdcall* PFN_eciRegisterCallback)(void* eci, PFN_EciCallback cb, void* data);
typedef int   (__stdcall* PFN_eciSetOutputBuffer)(void* eci, int samples, short* buffer);
typedef int   (__stdcall* PFN_eciSetParam)(void* eci, int param, int value);
typedef int   (__stdcall* PFN_eciSetVoiceParam)(void* eci, int voice, int param, int value);
typedef int   (__stdcall* PFN_eciCopyVoice)(void* eci, int from, int to);
typedef int   (__stdcall* PFN_eciAddText)(void* eci, const char* text);
typedef int   (__stdcall* PFN_eciInsertIndex)(void* eci, int index);
typedef int   (__stdcall* PFN_eciSynthesize)(void* eci);
typedef int   (__stdcall* PFN_eciSpeaking)(void* eci);

struct EciApi {
    PFN_eciNewEx            newEx;
    PFN_eciDelete           del;
    PFN_eciRegisterCallback registerCallback;
    PFN_eciSetOutputBuffer  setOutputBuffer;
    PFN_eciSetParam         setParam;
    PFN_eciSetVoiceParam    setVoiceParam;
    PFN_eciCopyVoice        copyVoice;
    PFN_eciAddText          addText;
    PFN_eciInsertIndex      insertIndex;
    PFN_eciSynthesize       synthesize;
    PFN_eciSpeaking         speaking;
};

EciApi g_api       = {};
bool   g_loadTried = false;
bool   g_loaded    = false;

// ---- Settings (from NVDA's saved config) ----------------------------------
// -1 = not set in nvda.ini: leave ECI's own (or the variant's) default.
struct Settings {
    int  voice       = -1;  // eciLanguageDialect, e.g. 262144 = German
    int  variant     = -1;  // preset copied onto voice 0
    int  rate        = -1;  // NVDA percent
    bool rateBoost   = false;
    int  pitch       = -1;
    int  inflection  = -1;
    int  headSize    = -1;
    int  roughness   = -1;
    int  breathiness = -1;
    int  sampleRate  = 1;   // driver default '1' = 11025 Hz
};

Settings ReadSettings(const nvda_config::Config& c) {
    Settings s;
    const char* sec = "speech";
    const char* sub = "ibmeci";
    s.voice       = c.FindInt(sec, sub, "voice", -1);
    s.variant     = c.FindInt(sec, sub, "variant", -1);
    s.rate        = c.FindInt(sec, sub, "rate", -1);
    const char* boost = c.Find(sec, sub, "rateBoost");
    s.rateBoost   = boost && std::string(boost) == "True";
    s.pitch       = c.FindInt(sec, sub, "pitch", -1);
    s.inflection  = c.FindInt(sec, sub, "inflection", -1);
    s.headSize    = c.FindInt(sec, sub, "hsz", -1);
    s.roughness   = c.FindInt(sec, sub, "rgh", -1);
    s.breathiness = c.FindInt(sec, sub, "bth", -1);
    s.sampleRate  = c.FindInt(sec, sub, "sampleRate", 1);
    return s;
}

// Engine folder: [ibmeci] TTSPath, relative to the add-on's synthDrivers
// folder unless absolute (the driver's setPathsFromConfig).
std::string EngineDir(const nvda_config::Config& c) {
    const char* p = c.Find("ibmeci", "", "TTSPath");
    std::string path = p ? p : "ibmtts";
    if (path.size() > 1 && (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'))) return path;
    return nvda_config::ConfigDir() + "\\addons\\IBMTTS\\synthDrivers\\" + path;
}

std::string EngineFile(const nvda_config::Config& c) {
    const char* dll = c.Find("ibmeci", "", "dllName");
    return EngineDir(c) + "\\" + (dll ? dll : "eci.dll");
}

template <typename T>
bool Resolve(HMODULE lib, T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(lib, name));
    if (!fn) acclog::Write("Eloquence", "missing export %s", name);
    return fn != nullptr;
}

bool EnsureLoaded(const nvda_config::Config& c) {
    if (g_loadTried) return g_loaded;
    g_loadTried = true;
    const std::string dir = EngineDir(c);
    const std::string dll = EngineFile(c);
    if (GetFileAttributesA(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        acclog::Write("Eloquence", "no installed engine at %s", dll.c_str());
        return false;
    }
    // Same order as the add-on: an optional etidev.dll first, then the engine,
    // loaded from its own folder so its .SYN files and ECI.INI are found.
    const std::string etidev = dir + "\\etidev.dll";
    if (GetFileAttributesA(etidev.c_str()) != INVALID_FILE_ATTRIBUTES) {
        LoadLibraryExA(etidev.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    HMODULE lib = LoadLibraryExA(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) {
        acclog::Write("Eloquence", "LoadLibrary(%s) failed err=%lu (a 64-bit engine cannot "
                      "load in the 32-bit game)", dll.c_str(), GetLastError());
        return false;
    }
    g_loaded =
        Resolve(lib, g_api.newEx,            "eciNewEx")            &&
        Resolve(lib, g_api.del,              "eciDelete")           &&
        Resolve(lib, g_api.registerCallback, "eciRegisterCallback") &&
        Resolve(lib, g_api.setOutputBuffer,  "eciSetOutputBuffer")  &&
        Resolve(lib, g_api.setParam,         "eciSetParam")         &&
        Resolve(lib, g_api.setVoiceParam,    "eciSetVoiceParam")    &&
        Resolve(lib, g_api.copyVoice,        "eciCopyVoice")        &&
        Resolve(lib, g_api.addText,          "eciAddText")          &&
        Resolve(lib, g_api.insertIndex,      "eciInsertIndex")      &&
        Resolve(lib, g_api.synthesize,       "eciSynthesize")       &&
        Resolve(lib, g_api.speaking,         "eciSpeaking");
    acclog::Write("Eloquence", "engine %s from %s", g_loaded ? "loaded" : "unusable", dll.c_str());
    return g_loaded;
}

// ---- Jobs and the worker --------------------------------------------------
// A job is owned by whoever holds it: the queue, the worker, the finished
// list, then the main thread that takes it. A blocking caller that times out
// abandons its job to the worker (leaked), never frees it under the worker.
struct Job {
    uint32_t           ticket = 0;
    Settings           settings;
    std::string        text;          // cp1252, backquotes removed
    std::vector<float> samples;
    bool               ended   = false;    // the end-mark index came back
    const char*        failure = nullptr;  // null on success
    DWORD              elapsedMs = 0;
    HANDLE             done    = nullptr;  // blocking callers only
};

CRITICAL_SECTION   g_lock;
HANDLE             g_wake      = nullptr;  // auto-reset: a job was queued
bool               g_started   = false;
std::deque<Job*>   g_queue;
std::deque<Job*>   g_finished;

// Worker-owned engine state.
short g_buffer[kBufferSamples];
void* g_eci       = nullptr;
int   g_eciVoice  = -2;   // dialect g_eci was created with
Job*  g_current   = nullptr;  // the line being synthesised; the callback's target

int __stdcall OnEciMessage(void* /*eci*/, int msg, long lParam, void* /*data*/) {
    Job* job = g_current;
    if (!job) return kDataProcessed;
    if (msg == kMsgWaveform) {
        for (long i = 0; i < lParam && i < kBufferSamples; ++i) {
            job->samples.push_back(g_buffer[i] / 32768.0f);
        }
    } else if (msg == kMsgIndexReply && lParam == kEndMark) {
        job->ended = true;
    }
    return kDataProcessed;
}

void SetVoiceParamIfSet(int param, int value) {
    if (value >= 0) g_api.setVoiceParam(g_eci, 0, param, value);
}

// One line on the worker's persistent handle. SEH-guarded; no destructible
// locals (C2712). A fault or timeout drops the handle so the next job
// starts from a fresh one.
void RunJob(Job* job) {
    const Settings& s = job->settings;
    __try {
        if (g_eci && g_eciVoice != s.voice) {
            g_api.del(g_eci);
            g_eci = nullptr;
        }
        if (!g_eci) {
            g_eci = g_api.newEx(s.voice >= 0 ? s.voice : 0);
            if (!g_eci) { job->failure = "eciNewEx returned no engine"; return; }
            g_eciVoice = s.voice;
            // ORDER MATTERS, as in the driver: callback first, then the
            // output buffer. Registering the callback after the buffer (once
            // per line) dropped the buffer, and ECI then played every line
            // ALOUD through the sound card — loud, centred, in real time —
            // while handing back no samples (test round 2026-10-27). So the
            // callback is registered once, here, and finds the current line
            // through g_current.
            g_api.registerCallback(g_eci, OnEciMessage, nullptr);
            if (!g_api.setOutputBuffer(g_eci, kBufferSamples, g_buffer)) {
                g_api.del(g_eci);
                g_eci = nullptr;
                job->failure = "eciSetOutputBuffer refused";
                return;
            }
            // The driver's "required to process audio properly" pair.
            g_api.setParam(g_eci, kParamSynthMode, 1);
            g_api.setParam(g_eci, kParamInputType, 1);
        }
        g_current = job;
        g_api.setParam(g_eci, kParamSampleRate, s.sampleRate);
        if (s.variant > 0) g_api.copyVoice(g_eci, s.variant, 0);
        if (s.rate >= 0) {
            int speed = static_cast<int>(s.rate / 100.0 * (kMaxRate - kMinRate) + kMinRate + 0.5);
            if (s.rateBoost) speed = static_cast<int>(speed * kRateBoostFactor + 0.5);
            g_api.setVoiceParam(g_eci, 0, kVoiceSpeed, speed);
        }
        SetVoiceParamIfSet(kVoicePitch,       s.pitch);
        SetVoiceParamIfSet(kVoiceInflection,  s.inflection);
        SetVoiceParamIfSet(kVoiceHeadSize,    s.headSize);
        SetVoiceParamIfSet(kVoiceRoughness,   s.roughness);
        SetVoiceParamIfSet(kVoiceBreathiness, s.breathiness);
        // Always full: NVDA's volume is NOT used. NVDA plays at full system
        // level, the game through its own mixer (SFX slider, group, 3D
        // falloff), so ECI volume 55 came out far quieter in game than the
        // same 55 in NVDA (tested 2026-10-27). The player sets the loudness
        // from the mod's own combat-speech volume.
        g_api.setVoiceParam(g_eci, 0, kVoiceVolume, kFullVolume);

        g_api.addText(g_eci, job->text.c_str());
        g_api.insertIndex(g_eci, kEndMark);
        g_api.synthesize(g_eci);

        // ECI posts its callbacks to this thread; pump until the end mark.
        const DWORD start = GetTickCount();
        MSG msg;
        while (!job->ended && GetTickCount() - start < kRenderTimeoutMs) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            g_api.speaking(g_eci);  // also lets ECI deliver pending callbacks
            if (!job->ended) Sleep(1);
        }
        g_current = nullptr;
        if (!job->ended) {
            job->failure = "synthesis did not finish in time";
            g_api.del(g_eci);
            g_eci = nullptr;
        } else if (job->samples.empty()) {
            // Finished but delivered nothing: the handle is in a bad state
            // (the buffer-loss symptom above). Start the next line fresh.
            job->failure = "synthesis delivered no audio";
            g_api.del(g_eci);
            g_eci = nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_current = nullptr;
        job->failure = "ECI faulted";
        g_eci = nullptr;  // state unknown; leak it rather than touch it again
    }
}

DWORD WINAPI WorkerThread(void*) {
    MSG msg;
    PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create the queue
    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        for (;;) {
            Job* job = nullptr;
            EnterCriticalSection(&g_lock);
            if (!g_queue.empty()) { job = g_queue.front(); g_queue.pop_front(); }
            LeaveCriticalSection(&g_lock);
            if (!job) break;

            const DWORD start = GetTickCount();
            RunJob(job);
            job->elapsedMs = GetTickCount() - start;
            if (job->done) {
                SetEvent(job->done);  // the blocking caller owns it now
            } else {
                EnterCriticalSection(&g_lock);
                g_finished.push_back(job);
                LeaveCriticalSection(&g_lock);
            }
        }
    }
}

bool EnsureWorker() {
    if (g_started) return true;
    InitializeCriticalSection(&g_lock);
    g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE t = g_wake ? CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr) : nullptr;
    if (!t) {
        acclog::Write("Eloquence", "worker start failed err=%lu", GetLastError());
        return false;
    }
    CloseHandle(t);
    g_started = true;
    return true;
}

// Builds a job from NVDA's current saved settings and queues it. Null when
// the engine is unavailable.
Job* Enqueue(const char* text, uint32_t ticket, HANDLE done, int rateOffset) {
    if (!text || !*text) return nullptr;
    nvda_config::Config cfg;
    nvda_config::Load(cfg);
    if (!EnsureLoaded(cfg) || !EnsureWorker()) return nullptr;
    auto* job = new Job{};
    job->ticket   = ticket;
    job->settings = ReadSettings(cfg);
    job->settings.rate = prism::OffsetRatePercent(job->settings.rate, rateOffset);
    job->text     = text;
    // ECI reads backquotes as embedded commands; the driver strips them too.
    for (char& c : job->text) if (c == '`') c = ' ';
    job->done = done;
    EnterCriticalSection(&g_lock);
    g_queue.push_back(job);
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
    return job;
}

// Moves a finished job's audio into `out`. False (logged) on failure.
bool Collect(Job* job, prism::RenderedSpeech& out) {
    static const size_t kRates[] = {8000, 11025, 22050};
    const Settings& s = job->settings;
    out = prism::RenderedSpeech{};
    out.samples.swap(job->samples);
    out.channels   = 1;
    out.sampleRate = (s.sampleRate >= 0 && s.sampleRate <= 2) ? kRates[s.sampleRate] : 11025;
    if (job->failure || out.samples.empty()) {
        acclog::Write("Eloquence", "render failed: %s (%lu ms) [%s]",
                      job->failure ? job->failure : "no audio", job->elapsedMs,
                      job->text.c_str());
        out = prism::RenderedSpeech{};
        return false;
    }
    acclog::Write("Eloquence",
                  "render ok: %zu samples @ %zu Hz, %.2f s audio in %lu ms "
                  "(voice=%d variant=%d rate=%d%s pitch=%d inflection=%d) [%s]",
                  out.samples.size(), out.sampleRate,
                  static_cast<double>(out.samples.size()) / out.sampleRate, job->elapsedMs,
                  s.voice, s.variant, s.rate, s.rateBoost ? "+boost" : "", s.pitch,
                  s.inflection, job->text.c_str());
    return true;
}

}  // namespace

bool IsNvdaSynth() {
    nvda_config::Config cfg;
    if (!nvda_config::Load(cfg)) return false;
    const char* synth = cfg.Find("speech", "", "synth");
    if (!synth || std::string(synth) != "ibmeci") return false;
    return GetFileAttributesA(EngineFile(cfg).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool Submit(const char* text, uint32_t ticket, int rateOffset) {
    return Enqueue(text, ticket, nullptr, rateOffset) != nullptr;
}

bool TakeFinished(uint32_t& ticket, prism::RenderedSpeech& out, bool& ok) {
    if (!g_started) return false;
    Job* job = nullptr;
    EnterCriticalSection(&g_lock);
    if (!g_finished.empty()) { job = g_finished.front(); g_finished.pop_front(); }
    LeaveCriticalSection(&g_lock);
    if (!job) return false;
    ticket = job->ticket;
    ok     = Collect(job, out);
    delete job;
    return true;
}

bool RenderToMemory(const char* text, prism::RenderedSpeech& out) {
    out = prism::RenderedSpeech{};
    HANDLE done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!done) return false;
    Job* job = Enqueue(text, 0, done, 0);
    if (!job) { CloseHandle(done); return false; }
    if (WaitForSingleObject(done, kRenderTimeoutMs + 1000) != WAIT_OBJECT_0) {
        // The worker still owns the job and will SetEvent on `done` later;
        // leak both rather than free them under it. Only if ECI hangs.
        acclog::Write("Eloquence", "blocking render timed out; abandoned [%s]", text);
        return false;
    }
    CloseHandle(done);
    const bool ok = Collect(job, out);
    delete job;
    return ok;
}

}  // namespace acc::eloquence
