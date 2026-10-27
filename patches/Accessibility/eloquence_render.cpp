#include "eloquence_render.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "log.h"

namespace acc::eloquence {

namespace {

// ---- ECI ABI (32-bit, __stdcall, undecorated exports) ----------------------
// Constants as the IBMTTS driver declares them (_ibmeci.py).
constexpr int kParamSynthMode  = 0;
constexpr int kParamInputType  = 1;
constexpr int kParamSampleRate = 5;
constexpr int kVoiceHeadSize   = 1;
constexpr int kVoicePitch      = 2;   // eciPitchBaseline
constexpr int kVoiceInflection = 3;   // eciPitchFluctuation
constexpr int kVoiceRoughness  = 4;
constexpr int kVoiceBreathiness = 5;
constexpr int kVoiceSpeed      = 6;
constexpr int kVoiceVolume     = 7;
constexpr int kMsgWaveform     = 0;   // eciWaveformBuffer: lParam = samples ready
constexpr int kMsgIndexReply   = 2;   // eciIndexReply:     lParam = index value
constexpr int kDataProcessed   = 1;
constexpr int kFullVolume      = 100;  // eciVolume range is 0..100
constexpr int kEndMark         = 0xffff;  // index inserted after the text

// NVDA rate percent → ECI speed, as the driver's _percentToParam(40..156).
constexpr int    kMinRate         = 40;
constexpr int    kMaxRate         = 156;
constexpr double kRateBoostFactor = 1.6;

constexpr int   kBufferSamples = 3300;   // the driver's output-buffer size
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

// ---- NVDA config --------------------------------------------------------
// -1 = not set in nvda.ini: leave ECI's own (or the variant's) default.
struct Settings {
    int  voice      = -1;  // eciLanguageDialect, e.g. 262144 = German
    int  variant    = -1;  // preset copied onto voice 0
    int  rate       = -1;  // NVDA percent
    bool rateBoost  = false;
    int  pitch      = -1;
    int  inflection = -1;
    int  volume     = -1;
    int  headSize   = -1;
    int  roughness  = -1;
    int  breathiness = -1;
    int  sampleRate = 1;   // driver default '1' = 11025 Hz
    std::string ttsPath = "ibmtts";
    std::string dllName = "eci.dll";
};

std::string NvdaConfigDir() {
    char appData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", appData, sizeof(appData));
    if (n == 0 || n >= sizeof(appData)) return std::string();
    return std::string(appData) + "\\nvda";
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// nvda.ini is ConfigObj: "[section]", "[[subsection]]", "key = value".
bool ReadSettings(Settings& s) {
    const std::string path = NvdaConfigDir() + "\\nvda.ini";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) {
        acclog::Write("Eloquence", "no NVDA config at %s", path.c_str());
        return false;
    }
    std::string section, subsection;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.compare(0, 2, "[[") == 0) { subsection = t; continue; }
        if (t[0] == '[') { section = t; subsection.clear(); continue; }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(t.substr(0, eq));
        const std::string val = Trim(t.substr(eq + 1));
        const int num = std::atoi(val.c_str());
        if (section == "[ibmeci]" && subsection.empty()) {
            if (key == "TTSPath") s.ttsPath = val;
            else if (key == "dllName") s.dllName = val;
        } else if (section == "[speech]" && subsection == "[[ibmeci]]") {
            if      (key == "voice")      s.voice = num;
            else if (key == "variant")    s.variant = num;
            else if (key == "rate")       s.rate = num;
            else if (key == "rateBoost")  s.rateBoost = (val == "True");
            else if (key == "pitch")      s.pitch = num;
            else if (key == "inflection") s.inflection = num;
            else if (key == "volume")     s.volume = num;
            else if (key == "hsz")        s.headSize = num;
            else if (key == "rgh")        s.roughness = num;
            else if (key == "bth")        s.breathiness = num;
            else if (key == "sampleRate") s.sampleRate = num;
        }
    }
    std::fclose(f);
    return true;
}

// TTSPath is relative to the add-on's synthDrivers folder unless absolute.
std::string EngineDir(const Settings& s) {
    const std::string& p = s.ttsPath;
    if (p.size() > 1 && (p[1] == ':' || (p[0] == '\\' && p[1] == '\\'))) return p;
    return NvdaConfigDir() + "\\addons\\IBMTTS\\synthDrivers\\" + p;
}

template <typename T>
bool Resolve(HMODULE lib, T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(lib, name));
    if (!fn) acclog::Write("Eloquence", "missing export %s", name);
    return fn != nullptr;
}

bool EnsureLoaded(const Settings& s) {
    if (g_loadTried) return g_loaded;
    g_loadTried = true;
    const std::string dir = EngineDir(s);
    const std::string dll = dir + "\\" + s.dllName;
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

// ---- Render job ---------------------------------------------------------
// Heap-allocated and handed to the worker thread. Self-contained (its own
// copies of settings, text and samples) so that on a timeout the caller can
// abandon it to a thread that may still write to it — it is leaked then.
struct RenderJob {
    Settings           settings;
    std::string        text;      // cp1252, backquotes removed
    std::vector<float> samples;
    short              buffer[kBufferSamples] = {};
    bool               ended   = false;    // the end-mark index came back
    const char*        failure = nullptr;  // null on success
};

int __stdcall OnEciMessage(void* /*eci*/, int msg, long lParam, void* data) {
    auto* job = static_cast<RenderJob*>(data);
    if (msg == kMsgWaveform) {
        for (long i = 0; i < lParam && i < kBufferSamples; ++i) {
            job->samples.push_back(job->buffer[i] / 32768.0f);
        }
    } else if (msg == kMsgIndexReply && lParam == kEndMark) {
        job->ended = true;
    }
    return kDataProcessed;
}

void SetVoiceParamIfSet(void* eci, int param, int value) {
    if (value >= 0) g_api.setVoiceParam(eci, 0, param, value);
}

// SEH-guarded ECI session. No destructible locals (C2712).
void RunEci(RenderJob* job) {
    const Settings& s = job->settings;
    void* eci = nullptr;
    __try {
        eci = g_api.newEx(s.voice >= 0 ? s.voice : 0);
        if (!eci) { job->failure = "eciNewEx returned no engine"; return; }
        g_api.registerCallback(eci, OnEciMessage, job);
        g_api.setOutputBuffer(eci, kBufferSamples, job->buffer);
        // The driver's "required to process audio properly" pair.
        g_api.setParam(eci, kParamSynthMode, 1);
        g_api.setParam(eci, kParamInputType, 1);
        g_api.setParam(eci, kParamSampleRate, s.sampleRate);
        if (s.variant > 0) g_api.copyVoice(eci, s.variant, 0);
        if (s.rate >= 0) {
            int speed = static_cast<int>(s.rate / 100.0 * (kMaxRate - kMinRate) + kMinRate + 0.5);
            if (s.rateBoost) speed = static_cast<int>(speed * kRateBoostFactor + 0.5);
            g_api.setVoiceParam(eci, 0, kVoiceSpeed, speed);
        }
        SetVoiceParamIfSet(eci, kVoicePitch,       s.pitch);
        SetVoiceParamIfSet(eci, kVoiceInflection,  s.inflection);
        // Always full: NVDA's volume is NOT baked in. NVDA plays at full
        // system level, the game through its own mixer (SFX slider, group,
        // 3D falloff), so ECI volume 55 came out far quieter in game than the
        // same 55 in NVDA (tested 2026-10-27). The setting is handed back as
        // RenderedSpeech::volume and applied as the per-source game volume.
        g_api.setVoiceParam(eci, 0, kVoiceVolume, kFullVolume);
        SetVoiceParamIfSet(eci, kVoiceHeadSize,    s.headSize);
        SetVoiceParamIfSet(eci, kVoiceRoughness,   s.roughness);
        SetVoiceParamIfSet(eci, kVoiceBreathiness, s.breathiness);

        g_api.addText(eci, job->text.c_str());
        g_api.insertIndex(eci, kEndMark);
        g_api.synthesize(eci);

        // ECI posts its callbacks to this thread; pump until the end mark.
        const DWORD start = GetTickCount();
        MSG msg;
        while (!job->ended && GetTickCount() - start < kRenderTimeoutMs) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            g_api.speaking(eci);  // also lets ECI deliver pending callbacks
            if (!job->ended) Sleep(1);
        }
        if (!job->ended) job->failure = "synthesis did not finish in time";
        g_api.del(eci);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        job->failure = "ECI faulted";
    }
}

DWORD WINAPI RenderThread(void* param) {
    MSG msg;
    PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create the queue
    RunEci(static_cast<RenderJob*>(param));
    return 0;
}

}  // namespace

bool RenderToMemory(const char* text, prism::RenderedSpeech& out) {
    out = prism::RenderedSpeech{};
    if (!text || !*text) return false;

    // Settings are re-read every call: the user may change NVDA's voice.
    auto* job = new RenderJob{};
    if (!ReadSettings(job->settings) || !EnsureLoaded(job->settings)) {
        delete job;
        return false;
    }
    // ECI reads backquotes as embedded commands; the driver strips them too.
    job->text = text;
    for (char& c : job->text) if (c == '`') c = ' ';

    const DWORD start = GetTickCount();
    HANDLE thread = CreateThread(nullptr, 0, RenderThread, job, 0, nullptr);
    if (!thread) {
        delete job;
        acclog::Write("Eloquence", "CreateThread failed err=%lu", GetLastError());
        return false;
    }
    if (WaitForSingleObject(thread, kRenderTimeoutMs + 1000) != WAIT_OBJECT_0) {
        // The thread still owns `job`; leak it rather than free memory a hung
        // ECI call may yet write to.
        CloseHandle(thread);
        acclog::Write("Eloquence", "render thread hung; abandoned [%s]", text);
        return false;
    }
    CloseHandle(thread);
    const DWORD elapsed = GetTickCount() - start;

    static const size_t kRates[] = {8000, 11025, 22050};
    const Settings& s = job->settings;
    const char* failure = job->failure;
    out.samples.swap(job->samples);
    out.volume     = s.volume >= 0 ? s.volume / 100.0f : 1.0f;
    out.channels   = 1;
    out.sampleRate = (s.sampleRate >= 0 && s.sampleRate <= 2) ? kRates[s.sampleRate] : 11025;
    if (failure || out.samples.empty()) {
        acclog::Write("Eloquence", "render failed: %s (%zu samples, %lu ms)",
                      failure ? failure : "no audio", out.samples.size(), elapsed);
        out = prism::RenderedSpeech{};
        delete job;
        return false;
    }
    acclog::Write("Eloquence",
                  "render ok: %zu samples @ %zu Hz, %.2f s audio in %lu ms "
                  "(voice=%d variant=%d rate=%d%s pitch=%d inflection=%d volume=%d) [%s]",
                  out.samples.size(), out.sampleRate,
                  static_cast<double>(out.samples.size()) / out.sampleRate, elapsed,
                  s.voice, s.variant, s.rate, s.rateBoost ? "+boost" : "", s.pitch,
                  s.inflection, s.volume, text);
    delete job;
    return true;
}

}  // namespace acc::eloquence
