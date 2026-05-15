// Speech bridge — Prism-only.
//
// Historical note: this module was originally a Tolk shim and kept the
// `tolk::` namespace name through the migration. After 2026-05-15 it
// no longer loads or links Tolk.dll. The two dispatch paths are:
//
//   Speak / Silence       → "normal" backend (NVDA / JAWS / ZDSR / OneCore /
//                            ... whichever screen reader Prism picks at
//                            startup; acquire_best). Same priority NVDA
//                            already used to be on under the Tolk path,
//                            so behaviour is preserved for typed-character
//                            cancel etc.
//   SpeakUrgent           → SAPI backend (always). Bypasses NVDA's typed-
//                            character cancel — exactly the bypass we
//                            already had via Prism for map-cursor cues.
//
// All Prism functions are resolved at runtime via LoadLibrary +
// GetProcAddress so a missing prism.dll degrades silently (no speech),
// same fault-tolerance posture the Tolk path had.
#include "tolk.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "log.h"

namespace tolk {

namespace {

// Subset of Prism's C ABI we use. Matches third_party/prism-dist/include/prism.h
// verbatim. We declare the function-pointer types here rather than including
// prism.h so the patch DLL build has no header dependency on the upstream tree.
struct PrismConfig { uint8_t version; };
typedef struct PrismContext PrismContext;
typedef struct PrismBackend PrismBackend;
typedef uint64_t PrismBackendId;
typedef int PrismError;  // 0 == PRISM_OK

constexpr PrismError kPrismOk                     = 0;
constexpr PrismError kPrismErrAlreadyInitialized  = 15;  // see PrismError enum
constexpr uint64_t  kPrismBackendIdSapi           = 0x1D6DF72422CEEE66ull;

// SAPI speech rate for the urgent channel. Prism maps [0.0..1.0] to SAPI's
// -10..+10 with 0.5 = SAPI default (midpoint-linear). 0.8 lands at roughly
// SAPI +6 — distinctly faster than out-of-the-box SAPI so urgent map-cursor /
// region-cursor / walking cues don't drag during continuous WASD panning,
// still well within intelligibility for the bundled Microsoft voices.
constexpr float kPrismSapiUrgentRate = 0.8f;

typedef PrismConfig    (__cdecl* PFN_prism_config_init)(void);
typedef PrismContext*  (__cdecl* PFN_prism_init)(PrismConfig*);
typedef void           (__cdecl* PFN_prism_shutdown)(PrismContext*);
typedef bool           (__cdecl* PFN_prism_registry_exists)(PrismContext*, PrismBackendId);
typedef PrismBackend*  (__cdecl* PFN_prism_registry_acquire)(PrismContext*, PrismBackendId);
typedef PrismBackend*  (__cdecl* PFN_prism_registry_acquire_best)(PrismContext*);
typedef PrismError     (__cdecl* PFN_prism_backend_initialize)(PrismBackend*);
typedef PrismError     (__cdecl* PFN_prism_backend_speak)(PrismBackend*, const char*, bool);
typedef PrismError     (__cdecl* PFN_prism_backend_stop)(PrismBackend*);
typedef PrismError     (__cdecl* PFN_prism_backend_set_rate)(PrismBackend*, float);
typedef const char*    (__cdecl* PFN_prism_backend_name)(PrismBackend*);

HMODULE       g_prismLib       = nullptr;
PrismContext* g_prismCtx       = nullptr;
PrismBackend* g_prismNormal    = nullptr;   // NVDA / JAWS / OneCore / ... — chosen by acquire_best
PrismBackend* g_prismSapi      = nullptr;   // urgent channel; SAPI bypass
bool          g_initTried      = false;
bool          g_normalReady    = false;
bool          g_sapiReady      = false;

// L"NVDA" / L"SAPI" / ... — filled at Init time so ActiveScreenReader can
// hand back a stable wide-string pointer without doing the conversion on
// every call. L"none" if no backend resolved.
wchar_t g_activeNameW[64] = L"none";

PFN_prism_config_init           pPrism_config_init            = nullptr;
PFN_prism_init                  pPrism_init                   = nullptr;
PFN_prism_shutdown              pPrism_shutdown               = nullptr;
PFN_prism_registry_exists       pPrism_registry_exists        = nullptr;
PFN_prism_registry_acquire      pPrism_registry_acquire       = nullptr;
PFN_prism_registry_acquire_best pPrism_registry_acquire_best  = nullptr;
PFN_prism_backend_initialize    pPrism_backend_initialize     = nullptr;
PFN_prism_backend_speak         pPrism_backend_speak          = nullptr;
PFN_prism_backend_stop          pPrism_backend_stop           = nullptr;
PFN_prism_backend_set_rate      pPrism_backend_set_rate       = nullptr;
PFN_prism_backend_name          pPrism_backend_name           = nullptr;

template <typename T>
bool Resolve(HMODULE lib, T& fn, const char* name, bool required = true) {
    fn = reinterpret_cast<T>(GetProcAddress(lib, name));
    if (!fn && required) {
        acclog::Write("Speech", "GetProcAddress(%s) failed", name);
        return false;
    }
    return fn != nullptr;
}

// CP_ACP (Windows-1252 on the German build, current default on en-US) → UTF-8.
// Prism's SAPI backend strict-validates UTF-8 and returns PRISM_ERROR_INVALID_UTF8
// on any non-UTF-8 lead byte. The NVDA backend also strings text through
// simdutf-style validators in places. KOTOR's CExoString payload is whatever
// the OS active codepage is, so any non-ASCII byte (German umlauts etc.) is an
// invalid UTF-8 lead → whole utterance dropped without this re-encode.
//
// Writes a NUL-terminated UTF-8 string into `outBuf`. Returns true on success.
bool ReencodeAcpToUtf8(const char* in, char* outBuf, size_t outBufSize) {
    if (!in || !outBuf || outBufSize == 0) return false;
    int wideLen = MultiByteToWideChar(CP_ACP, 0, in, -1, nullptr, 0);
    if (wideLen <= 0) return false;
    wchar_t wStack[256];
    wchar_t* w = wStack;
    wchar_t* wHeap = nullptr;
    if (static_cast<size_t>(wideLen) > sizeof(wStack) / sizeof(wStack[0])) {
        wHeap = (wchar_t*)malloc(static_cast<size_t>(wideLen) * sizeof(wchar_t));
        if (!wHeap) return false;
        w = wHeap;
    }
    int gotWide = MultiByteToWideChar(CP_ACP, 0, in, -1, w, wideLen);
    if (gotWide <= 0) {
        if (wHeap) free(wHeap);
        return false;
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0 || static_cast<size_t>(needed) > outBufSize) {
        if (wHeap) free(wHeap);
        return false;
    }
    int got = WideCharToMultiByte(CP_UTF8, 0, w, -1, outBuf,
                                  static_cast<int>(outBufSize),
                                  nullptr, nullptr);
    if (wHeap) free(wHeap);
    return got > 0;
}

bool WideToUtf8(const wchar_t* in, char* outBuf, size_t outBufSize) {
    if (!in || !outBuf || outBufSize == 0) return false;
    int needed = WideCharToMultiByte(CP_UTF8, 0, in, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0 || static_cast<size_t>(needed) > outBufSize) return false;
    int got = WideCharToMultiByte(CP_UTF8, 0, in, -1, outBuf,
                                  static_cast<int>(outBufSize),
                                  nullptr, nullptr);
    return got > 0;
}

// Acquire the SAPI backend explicitly and bump its rate. Returns true iff the
// backend is fully initialised and ready to receive prism_backend_speak.
bool TryAcquireSapi() {
    if (!pPrism_registry_exists(g_prismCtx, kPrismBackendIdSapi)) {
        acclog::Write("Speech",
                      "SAPI backend not in registry; SpeakUrgent will fall "
                      "back to the normal backend");
        return false;
    }
    g_prismSapi = pPrism_registry_acquire(g_prismCtx, kPrismBackendIdSapi);
    if (!g_prismSapi) {
        acclog::Write("Speech", "prism_registry_acquire(SAPI) returned NULL");
        return false;
    }
    PrismError rc = pPrism_backend_initialize(g_prismSapi);
    // acquire_best initialises eagerly; explicit acquire requires initialize().
    // ALREADY_INITIALIZED is non-fatal — we just got a cached instance.
    if (rc != kPrismOk && rc != kPrismErrAlreadyInitialized) {
        acclog::Write("Speech",
                      "prism_backend_initialize(SAPI) failed rc=%d", rc);
        return false;
    }

    if (pPrism_backend_set_rate) {
        PrismError rrc = pPrism_backend_set_rate(g_prismSapi,
                                                 kPrismSapiUrgentRate);
        if (rrc != kPrismOk) {
            acclog::Write("Speech",
                          "prism_backend_set_rate(SAPI, %.2f) rc=%d "
                          "(SAPI staying at default rate)",
                          kPrismSapiUrgentRate, rrc);
        } else {
            acclog::Write("Speech",
                          "SAPI rate set to %.2f "
                          "(0.0=slowest, 0.5=default, 1.0=fastest)",
                          kPrismSapiUrgentRate);
        }
    }
    return true;
}

// Pick the best-available screen-reader / TTS backend for the normal channel.
// acquire_best already returns the first registered backend whose
// initialize() succeeds, in priority order (NVDA / JAWS / Window-Eyes / ZDSR /
// System Access / OneCore / SAPI / ...). Backends obtained via acquire_best
// are already initialised per Prism's docs.
bool TryAcquireNormal() {
    g_prismNormal = pPrism_registry_acquire_best(g_prismCtx);
    if (!g_prismNormal) {
        acclog::Write("Speech",
                      "prism_registry_acquire_best returned NULL — no "
                      "speech backend available; running silent");
        return false;
    }
    return true;
}

}  // namespace

bool Init() {
    if (g_initTried) return g_normalReady;
    g_initTried = true;

    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) {
        acclog::Write("Speech", "patch dir unknown; aborting init");
        return false;
    }

    char path[MAX_PATH];
    int written = snprintf(path, sizeof(path), "%s\\prism.dll", patchDir);
    if (written <= 0 || written >= (int)sizeof(path)) {
        acclog::Write("Speech", "prism.dll path overflow");
        return false;
    }

    // Point the DLL search path at our dir so prism.dll's delay-loaded
    // bridges (orca, speech_dispatcher, etc.) resolve against our bundled
    // neighbours instead of whatever is in WindowsApps. Restored after the
    // LoadLibrary call.
    char prevDir[MAX_PATH] = {0};
    DWORD prevLen = GetDllDirectoryA(MAX_PATH, prevDir);
    SetDllDirectoryA(patchDir);

    g_prismLib = LoadLibraryA(path);
    if (!g_prismLib) {
        acclog::Write("Speech",
                      "LoadLibrary(%s) failed err=%lu; running silent",
                      path, GetLastError());
        SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
        return false;
    }
    SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);

    bool ok =
        Resolve(g_prismLib, pPrism_config_init,           "prism_config_init")           &&
        Resolve(g_prismLib, pPrism_init,                  "prism_init")                  &&
        Resolve(g_prismLib, pPrism_shutdown,              "prism_shutdown")              &&
        Resolve(g_prismLib, pPrism_registry_exists,       "prism_registry_exists")       &&
        Resolve(g_prismLib, pPrism_registry_acquire,      "prism_registry_acquire")      &&
        Resolve(g_prismLib, pPrism_registry_acquire_best, "prism_registry_acquire_best") &&
        Resolve(g_prismLib, pPrism_backend_initialize,    "prism_backend_initialize")    &&
        Resolve(g_prismLib, pPrism_backend_speak,         "prism_backend_speak")         &&
        Resolve(g_prismLib, pPrism_backend_stop,          "prism_backend_stop")          &&
        Resolve(g_prismLib, pPrism_backend_name,          "prism_backend_name");
    if (!ok) return false;
    // set_rate is optional — older Prism builds may lack the export, and
    // backends without SUPPORTS_SET_RATE return an error at call time.
    (void)Resolve(g_prismLib, pPrism_backend_set_rate, "prism_backend_set_rate",
                  /*required=*/false);

    PrismConfig cfg = pPrism_config_init();
    g_prismCtx = pPrism_init(&cfg);
    if (!g_prismCtx) {
        acclog::Write("Speech", "prism_init returned NULL; running silent");
        return false;
    }

    g_normalReady = TryAcquireNormal();
    g_sapiReady   = TryAcquireSapi();

    if (g_normalReady) {
        const char* nameA = pPrism_backend_name(g_prismNormal);
        if (!nameA) nameA = "?";
        // Cache wide name for ActiveScreenReader() callers.
        MultiByteToWideChar(CP_UTF8, 0, nameA, -1, g_activeNameW,
                            sizeof(g_activeNameW) / sizeof(g_activeNameW[0]));
        acclog::Write("Speech",
                      "ready — normal backend = %s, urgent backend = %s",
                      nameA, g_sapiReady ? "SAPI" : "(SAPI unavailable, "
                                                    "urgent falls back to normal)");
    }

    return g_normalReady;
}

bool IsAvailable() {
    return g_normalReady;
}

// Self-log every Speak call so the patch log carries one canonical record of
// what the screen reader was asked to say, in chronological order. "[!]"
// prefix marks interrupt=true so the audit shows when speech preempted
// in-flight output. Trace dedups consecutive identical strings.
void Speak(const wchar_t* text, bool interrupt) {
    if (!g_normalReady || !text || !*text) return;

    // Audit log: convert to UTF-8 once so long strings print in full.
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
        acclog::Trace("Speech.spoke", "%s%s", interrupt ? "[!] " : "", audit);
    }

    // Speech path: re-use the UTF-8 buffer we just built for the audit log
    // when it succeeded; otherwise allocate a fresh small one. Prism backends
    // expect UTF-8.
    if (conv > 0) {
        PrismError rc = pPrism_backend_speak(g_prismNormal, audit, interrupt);
        if (rc != kPrismOk) {
            acclog::Trace("Speech",
                          "prism_backend_speak(normal) rc=%d; dropping utterance",
                          rc);
        }
    }
    if (heap_audit) free(heap_audit);
}

void Speak(const char* text, bool interrupt) {
    if (!g_normalReady || !text || !*text) return;
    acclog::Trace("Speech.spoke", "%s%s", interrupt ? "[!] " : "", text);

    // CP_ACP → UTF-8 via heap when needed. Chargen skill descriptions are
    // ~600+ ANSI chars, so we size dynamically.
    char stack_buf[1024];
    char* buf = stack_buf;
    char* heap_buf = nullptr;
    // 4× worst-case expansion (CP_ACP codepoints emit at most 3 UTF-8 bytes;
    // we keep a small safety margin) + NUL.
    size_t need_cap = strlen(text) * 4 + 1;
    if (need_cap > sizeof(stack_buf)) {
        heap_buf = (char*)malloc(need_cap);
        if (!heap_buf) return;
        buf = heap_buf;
    }
    if (ReencodeAcpToUtf8(text, buf, heap_buf ? need_cap : sizeof(stack_buf))) {
        PrismError rc = pPrism_backend_speak(g_prismNormal, buf, interrupt);
        if (rc != kPrismOk) {
            acclog::Trace("Speech",
                          "prism_backend_speak(normal) rc=%d; dropping utterance",
                          rc);
        }
    }
    if (heap_buf) free(heap_buf);
}

void SpeakUrgent(const char* text) {
    if (!g_normalReady || !text || !*text) return;

    // Re-encode CP_ACP → UTF-8 before dispatch. See ReencodeAcpToUtf8 doc
    // comment for the rc=13 (INVALID_UTF8) failure mode this guards.
    char utf8Buf[512];
    const char* speakText = text;
    if (ReencodeAcpToUtf8(text, utf8Buf, sizeof(utf8Buf))) {
        speakText = utf8Buf;
    }
    // If re-encode failed we still attempt the dispatch with the original
    // bytes — for pure-ASCII strings the byte sequence is identical to
    // UTF-8, so the dispatch succeeds and we don't lose the announce just
    // because the conversion happened to overflow the buffer.

    // Prefer SAPI: bypasses NVDA's typed-character cancel — the whole point
    // of this entry point. If SAPI didn't acquire (no Prism SAPI backend
    // registered on this system, init failed, etc.) fall back to the normal
    // backend so the announce still lands somewhere.
    PrismBackend* target = g_sapiReady ? g_prismSapi : g_prismNormal;
    const char*   tag    = g_sapiReady ? "[SAPI]"    : "[normal-fallback]";

    PrismError rc = pPrism_backend_speak(target, speakText, /*interrupt=*/true);
    if (rc == kPrismOk) {
        // Log the original CP_ACP text so the patch log stays readable
        // in the same encoding as every other log line.
        acclog::Trace("Speech.spoke", "%s %s", tag, text);
    } else {
        // Do NOT fall back to the normal backend on a SAPI rc!=0. SAPI
        // dispatches asynchronously; by the time rc!=0 returns SAPI may
        // already be mid-utterance, and routing the same string to the
        // normal backend would produce the double-speak observed in
        // patch-20260512-152803.log. Drop the utterance; the next
        // SpeakUrgent call retries naturally.
        acclog::Trace("Speech",
                      "SpeakUrgent: prism_backend_speak rc=%d; "
                      "dropping utterance (no double-speak fallback)", rc);
    }
}

void Silence() {
    if (!g_normalReady) return;
    acclog::Trace("Speech.spoke", "(silenced)");
    if (g_prismNormal) (void)pPrism_backend_stop(g_prismNormal);
    if (g_prismSapi)   (void)pPrism_backend_stop(g_prismSapi);
}

const wchar_t* ActiveScreenReader() {
    if (!g_normalReady) return L"none";
    return g_activeNameW;
}

void Shutdown() {
    // Deliberately do not free backends / shut down the context here —
    // Prism's bridge DLLs talk to RPC + COM objects, and tearing those down
    // inside DLL_PROCESS_DETACH risks the loader lock + COM uninit ordering
    // issues we used to hit with Tolk. Process exit will reclaim everything.
    g_normalReady = false;
    g_sapiReady   = false;
}

}  // namespace tolk
