#include "tolk.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "log.h"

namespace tolk {

namespace {

// Tolk's C ABI (from third_party/tolk/include/Tolk.h). All cdecl. We declare
// the function-pointer types here instead of including Tolk.h so the build
// has no header dependency on the upstream tree.
typedef void          (__cdecl* PFN_Tolk_Load)();
typedef void          (__cdecl* PFN_Tolk_Unload)();
typedef bool          (__cdecl* PFN_Tolk_IsLoaded)();
typedef bool          (__cdecl* PFN_Tolk_HasSpeech)();
typedef bool          (__cdecl* PFN_Tolk_Output)(const wchar_t*, bool);
typedef bool          (__cdecl* PFN_Tolk_Silence)();
typedef const wchar_t*(__cdecl* PFN_Tolk_DetectScreenReader)();

// Prism (the C ABI from include/prism.h). We use Prism's SAPI backend to
// deliver urgent map-side speech via a path NVDA does not manage — so the
// announcement can't be cancelled by NVDA's typed-character interrupt. We
// keep the regular Tolk path for everything else (so NVDA stays the
// voice the user picked for the rest of the mod).
//
// All function pointers are resolved via LoadLibrary + GetProcAddress so
// the patch DLL build doesn't need to link against prism.lib — and a
// missing prism.dll degrades silently to the existing Tolk_Output path.
//
// Subset of the API we actually call. Matches prism.h verbatim; the
// full surface is documented in `third_party/prism-dist/include/prism.h`.

struct PrismConfig { uint8_t version; };
typedef struct PrismContext PrismContext;
typedef struct PrismBackend PrismBackend;
typedef uint64_t PrismBackendId;
typedef int PrismError;  // 0 == PRISM_OK

constexpr uint8_t kPrismConfigVersion        = 2;
constexpr uint64_t kPrismBackendIdSapi       = 0x1D6DF72422CEEE66ull;

typedef PrismConfig    (__cdecl* PFN_prism_config_init)(void);
typedef PrismContext*  (__cdecl* PFN_prism_init)(PrismConfig*);
typedef void           (__cdecl* PFN_prism_shutdown)(PrismContext*);
typedef bool           (__cdecl* PFN_prism_registry_exists)(PrismContext*, PrismBackendId);
typedef PrismBackend*  (__cdecl* PFN_prism_registry_acquire)(PrismContext*, PrismBackendId);
typedef PrismError     (__cdecl* PFN_prism_backend_initialize)(PrismBackend*);
typedef PrismError     (__cdecl* PFN_prism_backend_speak)(PrismBackend*, const char*, bool);
typedef PrismError     (__cdecl* PFN_prism_backend_stop)(PrismBackend*);

HMODULE g_lib       = nullptr;
bool    g_initTried = false;
bool    g_available = false;

HMODULE g_prismLib                            = nullptr;
PrismContext* g_prismCtx                      = nullptr;
PrismBackend* g_prismSapi                     = nullptr;
bool g_prismResolveTried                      = false;
bool g_prismSapiReady                         = false;
PFN_prism_config_init        pPrism_config_init        = nullptr;
PFN_prism_init               pPrism_init               = nullptr;
PFN_prism_shutdown           pPrism_shutdown           = nullptr;
PFN_prism_registry_exists    pPrism_registry_exists    = nullptr;
PFN_prism_registry_acquire   pPrism_registry_acquire   = nullptr;
PFN_prism_backend_initialize pPrism_backend_initialize = nullptr;
PFN_prism_backend_speak      pPrism_backend_speak      = nullptr;
PFN_prism_backend_stop       pPrism_backend_stop       = nullptr;

PFN_Tolk_Load                pTolk_Load                = nullptr;
PFN_Tolk_Unload              pTolk_Unload              = nullptr;
PFN_Tolk_IsLoaded            pTolk_IsLoaded            = nullptr;
PFN_Tolk_HasSpeech           pTolk_HasSpeech           = nullptr;
PFN_Tolk_Output              pTolk_Output              = nullptr;
PFN_Tolk_Silence             pTolk_Silence             = nullptr;
PFN_Tolk_DetectScreenReader  pTolk_DetectScreenReader  = nullptr;

template <typename T>
bool Resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(g_lib, name));
    if (!fn) {
        acclog::Write("Tolk", "GetProcAddress(%s) failed", name);
        return false;
    }
    return true;
}

// Forward decl — defined further down. Eager-called from Init() so the
// SAPI backend readiness is logged at startup.
bool TryResolvePrismSapi();

}  // namespace

bool Init() {
    if (g_initTried) return g_available;
    g_initTried = true;

    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) {
        acclog::Write("Tolk", "patch dir unknown; aborting init");
        return false;
    }

    // Build absolute paths. Tolk.dll lives next to our DLL; nvdaControllerClient32.dll
    // does too. Tolk_Load() does its own LoadLibrary on the driver client DLLs
    // using a bare name, so we point the DLL search path at our dir for the
    // duration of the call and restore it afterwards.
    char tolkPath[MAX_PATH];
    int written = snprintf(tolkPath, sizeof(tolkPath), "%s\\Tolk.dll", patchDir);
    if (written <= 0 || written >= (int)sizeof(tolkPath)) {
        acclog::Write("Tolk", "tolk.dll path overflow");
        return false;
    }

    char prevDir[MAX_PATH] = {0};
    DWORD prevLen = GetDllDirectoryA(MAX_PATH, prevDir);
    SetDllDirectoryA(patchDir);

    g_lib = LoadLibraryA(tolkPath);
    if (!g_lib) {
        acclog::Write("Tolk", "LoadLibrary(%s) failed err=%lu", tolkPath, GetLastError());
        SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
        return false;
    }

    bool ok =
        Resolve(pTolk_Load,                "Tolk_Load")               &&
        Resolve(pTolk_Unload,              "Tolk_Unload")             &&
        Resolve(pTolk_IsLoaded,            "Tolk_IsLoaded")           &&
        Resolve(pTolk_HasSpeech,           "Tolk_HasSpeech")          &&
        Resolve(pTolk_Output,              "Tolk_Output")             &&
        Resolve(pTolk_Silence,             "Tolk_Silence")            &&
        Resolve(pTolk_DetectScreenReader,  "Tolk_DetectScreenReader");

    if (!ok) {
        FreeLibrary(g_lib);
        g_lib = nullptr;
        SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
        return false;
    }

    pTolk_Load();
    g_available = pTolk_IsLoaded() && pTolk_HasSpeech();

    // Eagerly probe Prism so we know at startup whether the SAPI bypass
    // path is wired up. Without this the first probe runs lazily on the
    // first urgent-speech call, which means we don't see the success /
    // failure log line until a map-cursor scan actually hits a waypoint.
    (void)TryResolvePrismSapi();

    const wchar_t* name = pTolk_DetectScreenReader();
    if (g_available) {
        // wprintf into ANSI for the log; screen reader names are ASCII in practice.
        char nameA[64] = "?";
        if (name) WideCharToMultiByte(CP_UTF8, 0, name, -1, nameA, sizeof(nameA), nullptr, nullptr);
        acclog::Write("Tolk", "loaded, screen reader = %s", nameA);
    } else {
        acclog::Write("Tolk", "loaded, but no screen reader with speech detected (running silent)");
    }

    SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
    return g_available;
}

bool IsAvailable() {
    return g_available;
}

// Self-log every Speak call so the patch log carries one canonical record
// of what NVDA was asked to say, in chronological order. Trace dedups
// consecutive identical strings (the "control 5" placeholder fired 7 times
// in a row case) while still preserving the count via the (repeated Nx)
// flush on change. "[!]" prefix marks interrupt=true so the audit also
// shows when speech preempted in-flight output.
void Speak(const wchar_t* text, bool interrupt) {
    if (!g_available || !text) return;
    // Size the audit buffer to fit the input so long strings show in full
    // in patch.log instead of silently failing the conversion (the speech
    // path above is unaffected — pTolk_Output gets the original wide text).
    char stack_audit[512];
    char* audit = stack_audit;
    char* heap_audit = nullptr;
    int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                     nullptr, nullptr);
    if (needed > (int)sizeof(stack_audit)) {
        heap_audit = (char*)malloc((size_t)needed);
        if (heap_audit) audit = heap_audit;
        else needed = (int)sizeof(stack_audit);  // fall back: truncated log
    }
    int conv = WideCharToMultiByte(CP_UTF8, 0, text, -1, audit, needed,
                                   nullptr, nullptr);
    if (conv > 0) {
        acclog::Trace("Tolk.spoke", "%s%s", interrupt ? "[!] " : "", audit);
    }
    if (heap_audit) free(heap_audit);
    pTolk_Output(text, interrupt);
}

void Speak(const char* text, bool interrupt) {
    if (!g_available || !text || !*text) return;
    acclog::Trace("Tolk.spoke", "%s%s", interrupt ? "[!] " : "", text);
    // Size the wide buffer to fit the input — chargen-skill descriptions are
    // ~600+ ANSI chars, so a fixed 512-wchar buffer silently dropped the
    // longer ones (MultiByteToWideChar returned 0 / ERROR_INSUFFICIENT_BUFFER).
    wchar_t stack_buf[256];
    wchar_t* buf = stack_buf;
    wchar_t* heap_buf = nullptr;
    int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (needed <= 0) return;
    if ((size_t)needed > sizeof(stack_buf) / sizeof(stack_buf[0])) {
        heap_buf = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
        if (!heap_buf) return;
        buf = heap_buf;
    }
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, buf, needed);
    if (n > 0) pTolk_Output(buf, interrupt);
    if (heap_buf) free(heap_buf);
}

namespace {

bool TryResolvePrismSapi() {
    if (g_prismResolveTried) return g_prismSapiReady;
    g_prismResolveTried = true;

    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) return false;
    char path[MAX_PATH];
    int written = snprintf(path, sizeof(path), "%s\\prism.dll", patchDir);
    if (written <= 0 || written >= (int)sizeof(path)) return false;

    // Same DLL-search-path dance as Tolk_Load: prism.dll's own delay-
    // loaded backend bridges (orca, speech_dispatcher) use bare DLL
    // names; pointing the search path at our dir means they resolve
    // against bundled neighbours, not whatever's in WindowsApps.
    char prevDir[MAX_PATH] = {0};
    DWORD prevLen = GetDllDirectoryA(MAX_PATH, prevDir);
    SetDllDirectoryA(patchDir);

    g_prismLib = LoadLibraryA(path);
    if (!g_prismLib) {
        acclog::Write("Tolk",
                      "SpeakUrgent: prism.dll not loadable from %s (err=%lu); "
                      "falling back to Tolk_Output",
                      path, GetLastError());
        SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
        return false;
    }
    SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);

    auto resolveP = [](auto& fnPtr, const char* name) -> bool {
        fnPtr = reinterpret_cast<std::remove_reference_t<decltype(fnPtr)>>(
            GetProcAddress(g_prismLib, name));
        if (!fnPtr) {
            acclog::Write("Tolk", "SpeakUrgent: prism missing %s", name);
            return false;
        }
        return true;
    };

    if (!resolveP(pPrism_config_init,        "prism_config_init")        ||
        !resolveP(pPrism_init,               "prism_init")               ||
        !resolveP(pPrism_shutdown,           "prism_shutdown")           ||
        !resolveP(pPrism_registry_exists,    "prism_registry_exists")    ||
        !resolveP(pPrism_registry_acquire,   "prism_registry_acquire")   ||
        !resolveP(pPrism_backend_initialize, "prism_backend_initialize") ||
        !resolveP(pPrism_backend_speak,      "prism_backend_speak")      ||
        !resolveP(pPrism_backend_stop,       "prism_backend_stop")) {
        return false;
    }

    // prism_init wants a PrismConfig*. Initialise from prism_config_init
    // so we pick up the current version sentinel rather than guessing.
    PrismConfig cfg = pPrism_config_init();
    g_prismCtx = pPrism_init(&cfg);
    if (!g_prismCtx) {
        acclog::Write("Tolk", "SpeakUrgent: prism_init returned NULL");
        return false;
    }

    if (!pPrism_registry_exists(g_prismCtx, kPrismBackendIdSapi)) {
        acclog::Write("Tolk",
                      "SpeakUrgent: PRISM_BACKEND_SAPI not in registry; "
                      "SAPI fallback unavailable");
        return false;
    }

    g_prismSapi = pPrism_registry_acquire(g_prismCtx, kPrismBackendIdSapi);
    if (!g_prismSapi) {
        acclog::Write("Tolk", "SpeakUrgent: prism_registry_acquire(SAPI) returned NULL");
        return false;
    }

    PrismError rc = pPrism_backend_initialize(g_prismSapi);
    if (rc != 0) {
        acclog::Write("Tolk",
                      "SpeakUrgent: prism_backend_initialize(SAPI) failed rc=%d",
                      rc);
        return false;
    }

    g_prismSapiReady = true;
    acclog::Write("Tolk",
                  "SpeakUrgent: Prism SAPI backend ready — urgent speech "
                  "now bypasses NVDA's typed-character cancel");
    return true;
}

}  // namespace

void SpeakUrgent(const char* text) {
    if (!g_available || !text || !*text) return;

    if (TryResolvePrismSapi() && g_prismSapi) {
        // SAPI runs in a separate audio path that NVDA's input handlers
        // cannot cancel. interrupt=true so each new urgent message
        // replaces the previous (no queue pile-up while panning).
        PrismError rc = pPrism_backend_speak(g_prismSapi, text, /*interrupt=*/true);
        if (rc == 0) {
            acclog::Trace("Tolk.spoke", "[SAPI] %s", text);
            return;
        }
        acclog::Trace("Tolk",
                      "SpeakUrgent: prism_backend_speak rc=%d; "
                      "falling back to Tolk_Output for this utterance",
                      rc);
    }

    // Fallback path — NVDA route. Subject to typed-character cancel, but
    // at least the announcement attempts to dispatch. Same path as
    // Speak(const char*).
    int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (needed <= 0) return;
    wchar_t stack_buf[256];
    wchar_t* buf = stack_buf;
    wchar_t* heap_buf = nullptr;
    if ((size_t)needed > sizeof(stack_buf) / sizeof(stack_buf[0])) {
        heap_buf = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
        if (!heap_buf) return;
        buf = heap_buf;
    }
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, buf, needed);
    if (n > 0) {
        acclog::Trace("Tolk.spoke", "%s", text);
        pTolk_Output(buf, /*interrupt=*/false);
    }
    if (heap_buf) free(heap_buf);
}

void Silence() {
    if (!g_available) return;
    acclog::Trace("Tolk.spoke", "(silenced)");
    pTolk_Silence();
}

const wchar_t* ActiveScreenReader() {
    if (!g_available || !pTolk_DetectScreenReader) return L"none";
    const wchar_t* name = pTolk_DetectScreenReader();
    return name ? name : L"none";
}

void Shutdown() {
    if (g_lib && pTolk_Unload) {
        pTolk_Unload();
    }
    g_available = false;
    // Deliberately leak g_lib at process-shutdown — FreeLibrary inside
    // DLL_PROCESS_DETACH risks the loader lock + COM uninit ordering issues.
}

}  // namespace tolk
