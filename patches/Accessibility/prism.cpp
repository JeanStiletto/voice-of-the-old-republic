// Speech bridge — Prism only.
//
// Two dispatch paths:
//
//   Speak / Silence       → "normal" backend (NVDA / JAWS / ZDSR / OneCore /
//                            ... whichever screen reader Prism picks at
//                            startup; acquire_best). Same priority NVDA
//                            normally uses, so typed-character cancel etc.
//                            behaves the way the user expects from their
//                            screen reader.
//   SpeakUrgent           → SAPI backend (always). Bypasses NVDA's typed-
//                            character cancel — the bypass we rely on for
//                            map-cursor / compass / walking cues.
//
// All Prism functions are resolved at runtime via LoadLibrary +
// GetProcAddress so a missing prism.dll degrades silently (no speech).
#include "prism.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "log.h"
#include "mod_settings_store.h"  // persist the SAPI urgent-volume slider

namespace prism {

namespace {

// Subset of Prism's C ABI we use. Matches third_party/prism-dist/include/prism.h
// verbatim. We declare the function-pointer types here rather than including
// prism.h so the patch DLL build has no header dependency on the upstream tree.
typedef struct PrismContext PrismContext;
typedef struct PrismBackend PrismBackend;
typedef uint64_t PrismBackendId;
typedef int PrismError;  // 0 == PRISM_OK

constexpr PrismError kPrismOk                     = 0;
constexpr PrismError kPrismErrAlreadyInitialized  = 15;  // see PrismError enum
constexpr PrismError kPrismErrBackendNotAvailable = 16;
constexpr PrismError kPrismErrUnknown             = 17;  // sentinel for SEH faults
constexpr uint64_t  kPrismBackendIdSapi           = 0x1D6DF72422CEEE66ull;

// PrismBackendFeature bits. IS_SUPPORTED_AT_RUNTIME is set only while the
// backend's reader is genuinely reachable — every Windows backend recomputes it
// on each call (NVDA queries its RPC endpoint, SAPI asks COM for the SpVoice
// class object, ZDSR and BoyPC scan the process list). Unlike a successful
// initialize() it is trustworthy; PickNormal explains why that distinction
// decides whether the mod speaks at all.
constexpr uint64_t kPrismFeatureSupportedAtRuntime = 1ull << 0;
constexpr uint64_t kPrismFeatureSupportsSpeak      = 1ull << 2;

// MSVC delay-load helper exceptions. A backend whose vendor DLL is present but
// exports a mismatched symbol set raises PROC_NOT_FOUND; a missing DLL raises
// MOD_NOT_FOUND. Both are normally fatal — see the SEH guards below.
constexpr DWORD kVcppDelayLoadModNotFound  = 0xC06D007E;
constexpr DWORD kVcppDelayLoadProcNotFound = 0xC06D007F;

// SAPI speech rate for the urgent channel. Prism maps [0.0..1.0] to SAPI's
// -10..+10 with 0.5 = SAPI default (midpoint-linear). 0.8 lands at roughly
// SAPI +6 — distinctly faster than out-of-the-box SAPI so urgent map-cursor /
// region-cursor / walking cues don't drag during continuous WASD panning,
// still well within intelligibility for the bundled Microsoft voices.
constexpr float kPrismSapiUrgentRate = 0.8f;

// prism_init takes a void* and is always handed nullptr. It reads nothing from
// a NULL config and falls back to the global backend registry, which is exactly
// what we want — and that avoids declaring PrismConfig at all. That struct is
// version-specific in a way no single declaration survives: in 0.16.5 it was a
// lone version byte and prism_init required version == PRISM_CONFIG_VERSION
// exactly; in 0.17.3 it carries a registry pointer, an availability callback
// plus userdata, three uint32 tuning fields and a bool, and the check relaxed
// to ">". A config built for either version is rejected outright by the other.
// prism_config_init is deliberately not resolved for the same reason: it
// returns PrismConfig by value, so its calling convention changes shape with
// the struct — under 0.17.3 it returns through a hidden pointer this code
// would never pass, which corrupts the stack rather than failing cleanly.
typedef PrismContext*  (__cdecl* PFN_prism_init)(void*);
typedef void           (__cdecl* PFN_prism_shutdown)(PrismContext*);
typedef bool           (__cdecl* PFN_prism_registry_exists)(PrismContext*, PrismBackendId);
typedef size_t         (__cdecl* PFN_prism_registry_count)(PrismContext*);
typedef PrismBackendId (__cdecl* PFN_prism_registry_id_at)(PrismContext*, size_t);
typedef PrismBackend*  (__cdecl* PFN_prism_registry_acquire)(PrismContext*, PrismBackendId);
typedef PrismBackend*  (__cdecl* PFN_prism_registry_acquire_best)(PrismContext*);
typedef uint64_t       (__cdecl* PFN_prism_backend_get_features)(PrismBackend*);
typedef PrismError     (__cdecl* PFN_prism_backend_initialize)(PrismBackend*);
typedef PrismError     (__cdecl* PFN_prism_backend_speak)(PrismBackend*, const char*, bool);
typedef PrismError     (__cdecl* PFN_prism_backend_stop)(PrismBackend*);
typedef PrismError     (__cdecl* PFN_prism_backend_set_rate)(PrismBackend*, float);
typedef PrismError     (__cdecl* PFN_prism_backend_set_volume)(PrismBackend*, float);
typedef PrismError     (__cdecl* PFN_prism_backend_set_voice)(PrismBackend*, size_t);
typedef PrismError     (__cdecl* PFN_prism_backend_count_voices)(PrismBackend*, size_t*);
typedef const char*    (__cdecl* PFN_prism_backend_name)(PrismBackend*);

HMODULE       g_prismLib       = nullptr;
PrismContext* g_prismCtx       = nullptr;
PrismBackend* g_prismNormal    = nullptr;   // NVDA / JAWS / OneCore / ... — chosen by acquire_best
PrismBackend* g_prismSapi      = nullptr;   // urgent channel; SAPI bypass
bool          g_initTried      = false;
bool          g_normalReady    = false;
bool          g_sapiReady      = false;

// Tracks the SAPI voice id last applied via prism_backend_set_voice. SIZE_MAX
// = "unknown / not yet set"; first call will always issue set_voice so the
// backend's state matches our cache. Voice swap costs nothing in steady state
// — we only call set_voice when the requested id differs.
size_t g_sapiVoiceCurrent = static_cast<size_t>(-1);
// Cached voice count so out-of-range requests fall back silently instead of
// asking SAPI for a voice it doesn't have. 0 = not enumerated / no voices.
size_t g_sapiVoiceCount   = 0;

// User-facing SAPI urgent-channel volume, percent [0,100]; 100 = SAPI full.
// Backed by the persistent store (acc_settings.ini) so it survives relaunch,
// lazily pulled on first access. Prism maps the float arg of set_volume to
// [0.0..1.0] (same convention as set_rate), so the byte we hand over is
// percent/100. The slider in Mod-Einstellungen drives this via
// SetUrgentVolumePercent. (ApplySapiVolume lives below the function-pointer
// block — it needs pPrism_backend_set_volume.)
constexpr const char* kSapiVolumeKey   = "UrgentVolumePercent";
int  g_sapiVolumePercent = 100;
bool g_sapiVolumeLoaded  = false;

void EnsureSapiVolumeLoaded() {
    if (g_sapiVolumeLoaded) return;
    g_sapiVolumeLoaded = true;
    int v = acc::settings::GetInt(kSapiVolumeKey, 100);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    g_sapiVolumePercent = v;
}

// L"NVDA" / L"SAPI" / ... — filled at Init time so ActiveScreenReader can
// hand back a stable wide-string pointer without doing the conversion on
// every call. L"none" if no backend resolved.
wchar_t g_activeNameW[64] = L"none";

PFN_prism_init                  pPrism_init                   = nullptr;
PFN_prism_shutdown              pPrism_shutdown               = nullptr;
PFN_prism_registry_exists       pPrism_registry_exists        = nullptr;
PFN_prism_registry_count        pPrism_registry_count         = nullptr;
PFN_prism_registry_id_at        pPrism_registry_id_at         = nullptr;
PFN_prism_registry_acquire      pPrism_registry_acquire       = nullptr;
PFN_prism_registry_acquire_best pPrism_registry_acquire_best  = nullptr;
PFN_prism_backend_get_features  pPrism_backend_get_features   = nullptr;
PFN_prism_backend_initialize    pPrism_backend_initialize     = nullptr;
PFN_prism_backend_speak         pPrism_backend_speak          = nullptr;
PFN_prism_backend_stop          pPrism_backend_stop           = nullptr;
PFN_prism_backend_set_rate      pPrism_backend_set_rate       = nullptr;
PFN_prism_backend_set_volume    pPrism_backend_set_volume     = nullptr;
PFN_prism_backend_set_voice     pPrism_backend_set_voice      = nullptr;
PFN_prism_backend_count_voices  pPrism_backend_count_voices   = nullptr;
PFN_prism_backend_name          pPrism_backend_name           = nullptr;

// Push g_sapiVolumePercent to the live SAPI backend. Safe to call any time;
// no-ops if the backend isn't acquired yet or the export is missing (older
// Prism builds / backends without SUPPORTS_SET_VOLUME). Does NOT gate on
// g_sapiReady so it can run mid-acquire (when g_prismSapi is set but the
// ready flag hasn't flipped).
void ApplySapiVolume() {
    if (!g_prismSapi || !pPrism_backend_set_volume) return;
    EnsureSapiVolumeLoaded();
    float vol = static_cast<float>(g_sapiVolumePercent) / 100.0f;
    PrismError rc = pPrism_backend_set_volume(g_prismSapi, vol);
    if (rc != kPrismOk) {
        acclog::Write("Speech",
                      "prism_backend_set_volume(SAPI, %.2f) rc=%d "
                      "(SAPI staying at default volume)", vol, rc);
    } else {
        acclog::Write("Speech",
                      "SAPI urgent volume set to %d%% (%.2f, 0.0=mute, "
                      "1.0=full)", g_sapiVolumePercent, vol);
    }
}

template <typename T>
bool Resolve(HMODULE lib, T& fn, const char* name, bool required = true) {
    fn = reinterpret_cast<T>(GetProcAddress(lib, name));
    if (!fn && required) {
        acclog::Write("Speech", "GetProcAddress(%s) failed", name);
        return false;
    }
    return fn != nullptr;
}

// Active narrow-text codepage. Both our own string tables and the engine's
// CExoString payload are in the *game's* codepage, not necessarily the OS one:
// a Russian install is Windows-1251 even on a German or English Windows, where
// CP_ACP is 1252. Defaulting to CP_ACP keeps the pre-existing en/de/fr/it/es
// behaviour byte-for-byte (those are all 1252, which is what CP_ACP resolves to
// on their users' systems); SetSpeechCodepage pins it explicitly once the
// language is known. See DetectLanguageFromTlk in core_dllmain.cpp.
UINT g_speechCodepage = CP_ACP;

// Game codepage → UTF-8. Prism's SAPI backend strict-validates UTF-8 and
// returns PRISM_ERROR_INVALID_UTF8 on any non-UTF-8 lead byte. The NVDA backend
// also strings text through simdutf-style validators in places. So any
// non-ASCII byte (German umlauts, Cyrillic, ...) is an invalid UTF-8 lead →
// whole utterance dropped without this re-encode.
//
// Writes a NUL-terminated UTF-8 string into `outBuf`. Returns true on success.
bool ReencodeAcpToUtf8(const char* in, char* outBuf, size_t outBufSize) {
    if (!in || !outBuf || outBufSize == 0) return false;
    const UINT cp = g_speechCodepage;
    int wideLen = MultiByteToWideChar(cp, 0, in, -1, nullptr, 0);
    if (wideLen <= 0) return false;
    wchar_t wStack[256];
    wchar_t* w = wStack;
    wchar_t* wHeap = nullptr;
    if (static_cast<size_t>(wideLen) > sizeof(wStack) / sizeof(wStack[0])) {
        wHeap = (wchar_t*)malloc(static_cast<size_t>(wideLen) * sizeof(wchar_t));
        if (!wHeap) return false;
        w = wHeap;
    }
    int gotWide = MultiByteToWideChar(cp, 0, in, -1, w, wideLen);
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

// ---- Last-utterance capture ---------------------------------------------
//
// Held in the game codepage — the same bytes the caller handed us — so
// consumers can re-widen with the codepage of their choice. Sized well above
// kAnnounceTextMax (1024) so we never truncate an utterance the speech path
// itself carried in full; over-long text is truncated rather than dropped.
char s_lastSpoken[4096] = {0};
bool s_suppressNextCapture = false;

void CaptureSpoken(const char* text) {
    if (s_suppressNextCapture) {
        s_suppressNextCapture = false;
        return;
    }
    if (!text) return;
    strncpy_s(s_lastSpoken, text, _TRUNCATE);
}

// Wide path: fold back to the game codepage so LastSpoken() has one encoding
// regardless of which overload produced it. WC_NO_BEST_FIT_CHARS is
// deliberately NOT set — a best-fit approximation is better than losing the
// utterance for a codepoint the game codepage can't represent.
void CaptureSpokenW(const wchar_t* text) {
    if (s_suppressNextCapture) {
        s_suppressNextCapture = false;
        return;
    }
    if (!text) return;
    int got = WideCharToMultiByte(g_speechCodepage, 0, text, -1, s_lastSpoken,
                                  (int)sizeof(s_lastSpoken), nullptr, nullptr);
    if (got <= 0) s_lastSpoken[0] = '\0';
    s_lastSpoken[sizeof(s_lastSpoken) - 1] = '\0';
}

// ---- SEH guards around prism backend calls -----------------------------
//
// Some screen-reader backends delay-load a vendor DLL inside their
// initialize() path (ZDSR → ZDSRAPI.dll, PC-Talker → PCTKUSR.dll, BoyPC →
// BoyCtrl.dll, ...). When the user has that reader installed but at a DLL
// version whose export set doesn't match what prism.dll was built against,
// the MSVC delay-load helper raises a structured exception
// (0xC06D007F PROC_NOT_FOUND / 0xC06D007E MOD_NOT_FOUND). Unhandled it
// crashes the game at startup before any speech — reported by a pl-PL beta
// tester with ZDSR installed.
//
// The root fix lives in prism itself: BackendRegistry::acquire_best /
// create_best now SEH-guard each backend's initialize() and skip a faulting
// one (third_party/prism/source/backends/backend_registry.cpp). So we let
// prism own backend selection — the normal channel is a single acquire_best
// call and nothing here reimplements prism's priority walk. These wrappers
// remain for two narrow reasons: (1) a thin belt-and-suspenders net around
// that one acquire_best call, in case a deployed prism.dll predates the fix;
// (2) the explicit SAPI-acquire path for the urgent channel, which calls
// acquire + initialize directly. The helpers hold only PODs / raw pointers —
// MSVC forbids __try in a function that needs C++ object unwinding, and
// logging (which builds temporaries) must therefore stay in the callers, which
// read the returned fault code.
const char* SehCodeName(DWORD code) {
    switch (code) {
        case kVcppDelayLoadModNotFound:  return "delay-load module-not-found";
        case kVcppDelayLoadProcNotFound: return "delay-load proc-not-found";
        case EXCEPTION_ACCESS_VIOLATION: return "access-violation";
        default:                         return "structured-exception";
    }
}

// Returns acquire_best's backend, or null. *outCode is the SEH code if a fault
// was caught (0 on a clean null/success).
PrismBackend* SehAcquireBest(PrismContext* ctx, DWORD* outCode) {
    *outCode = 0;
    __try {
        return pPrism_registry_acquire_best(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outCode = GetExceptionCode();
        return nullptr;
    }
}

// Acquire one specific backend by id, SEH-guarded.
PrismBackend* SehAcquire(PrismContext* ctx, PrismBackendId id, DWORD* outCode) {
    *outCode = 0;
    __try {
        return pPrism_registry_acquire(ctx, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outCode = GetExceptionCode();
        return nullptr;
    }
}

// Initialize one backend, SEH-guarded. Returns the PrismError, or
// kPrismErrUnknown with *outCode set if a fault was caught.
PrismError SehInitialize(PrismBackend* be, DWORD* outCode) {
    *outCode = 0;
    __try {
        return pPrism_backend_initialize(be);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outCode = GetExceptionCode();
        return kPrismErrUnknown;
    }
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
    DWORD seh = 0;
    g_prismSapi = SehAcquire(g_prismCtx, kPrismBackendIdSapi, &seh);
    if (!g_prismSapi) {
        if (seh) {
            acclog::Write("Speech",
                          "prism_registry_acquire(SAPI) faulted (%s 0x%08lX)",
                          SehCodeName(seh), seh);
        } else {
            acclog::Write("Speech", "prism_registry_acquire(SAPI) returned NULL");
        }
        return false;
    }
    PrismError rc = SehInitialize(g_prismSapi, &seh);
    // acquire_best initialises eagerly; explicit acquire requires initialize().
    // ALREADY_INITIALIZED is non-fatal — we just got a cached instance.
    if (seh) {
        acclog::Write("Speech",
                      "prism_backend_initialize(SAPI) faulted (%s 0x%08lX)",
                      SehCodeName(seh), seh);
        g_prismSapi = nullptr;
        return false;
    }
    if (rc != kPrismOk && rc != kPrismErrAlreadyInitialized) {
        acclog::Write("Speech",
                      "prism_backend_initialize(SAPI) failed rc=%d", rc);
        g_prismSapi = nullptr;
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

    // Apply the persisted urgent-volume slider to the freshly-acquired
    // backend. EnsureSapiVolumeLoaded pulls the saved value (default 100%)
    // so the user's last choice carries across launches.
    ApplySapiVolume();

    // Cache voice count so SpeakUrgent(text, voiceId) can range-check before
    // calling set_voice — cheaper than the round trip on every utterance.
    if (pPrism_backend_count_voices) {
        size_t count = 0;
        PrismError vrc = pPrism_backend_count_voices(g_prismSapi, &count);
        if (vrc == kPrismOk) {
            g_sapiVoiceCount = count;
            acclog::Write("Speech", "SAPI voice count = %zu", count);
        } else {
            acclog::Write("Speech",
                          "prism_backend_count_voices(SAPI) rc=%d "
                          "(voice-id requests will fall back to default)",
                          vrc);
        }
    }
    return true;
}

// True iff this backend can speak AND its reader is reachable right now.
//
// A successful initialize() is not enough, which is the whole reason this
// check exists. Prism 0.16.5's NVDA backend returns success when NVDA is not
// running: its testIfRunning check sits behind an RPC interface query that
// itself fails when there is no server to answer, so with NVDA absent the
// check is skipped and start-up counts as successful. NVDA holds the highest
// priority of any backend, so it won acquire_best on every machine and then
// refused every utterance with BACKEND_NOT_AVAILABLE — total silence for
// anyone running JAWS, Narrator, ZDSR or nothing at all. Upstream fixed that
// in 0.17.x, but the gate stays: it costs one call, it is correct either way,
// and it is the only thing standing between a user and silence if a deployed
// prism.dll turns out to be older than the one we ship.
bool BackendIsUsable(PrismBackend* be) {
    if (!be || !pPrism_backend_get_features) return be != nullptr;
    const uint64_t want = kPrismFeatureSupportsSpeak | kPrismFeatureSupportedAtRuntime;
    return (pPrism_backend_get_features(be) & want) == want;
}

// Walk the registry ourselves and take the first backend that is live and
// initialises. Registry order is descending priority, so this reproduces
// acquire_best's ordering — which cannot simply be called again, because it
// caches its first pick and would hand back the same dead backend.
//
// Liveness is checked before initialize(), never after, so a reader that is
// not running never has its vendor client library touched. That matters here:
// this walk is the path that reaches ZDSR / PC-Talker / BoyPC, and skipping
// them outright is stronger than relying on the SEH guard to survive them.
PrismBackend* WalkForLiveBackend() {
    if (!pPrism_registry_count || !pPrism_registry_id_at || !pPrism_backend_get_features) {
        acclog::Write("Speech",
                      "prism.dll lacks the registry-walk exports; cannot look "
                      "past acquire_best's choice");
        return nullptr;
    }

    const size_t count = pPrism_registry_count(g_prismCtx);
    for (size_t i = 0; i < count; ++i) {
        const PrismBackendId id = pPrism_registry_id_at(g_prismCtx, i);
        if (id == 0) continue;

        DWORD seh = 0;
        PrismBackend* be = SehAcquire(g_prismCtx, id, &seh);
        if (!be) continue;
        if (!BackendIsUsable(be)) continue;

        const PrismError rc = SehInitialize(be, &seh);
        if (seh) {
            acclog::Write("Speech",
                          "prism_backend_initialize faulted for a live backend "
                          "(%s 0x%08lX); trying the next one",
                          SehCodeName(seh), seh);
            continue;
        }
        if (rc != kPrismOk && rc != kPrismErrAlreadyInitialized) {
            acclog::Write("Speech",
                          "backend reported itself live but initialize failed "
                          "rc=%d; trying the next one", rc);
            continue;
        }
        return be;
    }
    return nullptr;
}

// Pick the screen-reader / TTS backend for the normal channel. Prism's
// acquire_best gets first say — it owns the priority walk and skips a backend
// whose vendor DLL faults during initialize() — but whatever it returns still
// has to pass BackendIsUsable, and WalkForLiveBackend takes over when it does
// not. SAPI remains the universal catch-all at the bottom of the order, so a
// machine with no screen reader running still speaks.
bool TryAcquireNormal() {
    DWORD seh = 0;
    PrismBackend* best = SehAcquireBest(g_prismCtx, &seh);
    if (seh) {
        acclog::Write("Speech",
                      "prism_registry_acquire_best faulted (%s 0x%08lX); "
                      "walking the registry instead", SehCodeName(seh), seh);
    }

    if (best && BackendIsUsable(best)) {
        g_prismNormal = best;
        return true;
    }

    if (best) {
        const char* nameA = pPrism_backend_name ? pPrism_backend_name(best) : nullptr;
        acclog::Write("Speech",
                      "acquire_best chose %s but its reader is not running; "
                      "walking the registry", nameA ? nameA : "?");
    }

    g_prismNormal = WalkForLiveBackend();
    if (g_prismNormal) return true;

    acclog::Write("Speech", "no live speech backend available; running silent");
    return false;
}

// Make the SAPI backend speak with voice `voiceId`. Cached so consecutive
// calls with the same id are free. Out-of-range / unsupported silently no-op
// and leave the current voice in place. Returns false only on a hard set_voice
// error so the caller can log; the speak still proceeds with whatever voice
// SAPI currently has.
bool EnsureSapiVoice(size_t voiceId) {
    if (!g_sapiReady || !pPrism_backend_set_voice) return false;
    if (g_sapiVoiceCount != 0 && voiceId >= g_sapiVoiceCount) return false;
    if (voiceId == g_sapiVoiceCurrent) return true;
    PrismError rc = pPrism_backend_set_voice(g_prismSapi, voiceId);
    if (rc != kPrismOk) {
        acclog::Trace("Speech",
                      "prism_backend_set_voice(SAPI, %zu) rc=%d "
                      "(staying on voice %zu)", voiceId, rc, g_sapiVoiceCurrent);
        return false;
    }
    g_sapiVoiceCurrent = voiceId;
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
    // set_rate / set_voice / count_voices are optional — older Prism builds
    // may lack the exports, and backends without the matching SUPPORTS_*
    // capability return an error at call time. SpeakUrgent stays functional
    // either way; voice selection just no-ops.
    (void)Resolve(g_prismLib, pPrism_backend_set_rate,     "prism_backend_set_rate",
                  /*required=*/false);
    (void)Resolve(g_prismLib, pPrism_backend_set_volume,   "prism_backend_set_volume",
                  /*required=*/false);
    (void)Resolve(g_prismLib, pPrism_backend_set_voice,    "prism_backend_set_voice",
                  /*required=*/false);
    (void)Resolve(g_prismLib, pPrism_backend_count_voices, "prism_backend_count_voices",
                  /*required=*/false);
    // Optional so an older prism.dll still loads and speaks. Without them the
    // liveness gate degrades to acquire_best's unchecked answer — logged by
    // WalkForLiveBackend rather than failing silently.
    (void)Resolve(g_prismLib, pPrism_backend_get_features, "prism_backend_get_features",
                  /*required=*/false);
    (void)Resolve(g_prismLib, pPrism_registry_count,       "prism_registry_count",
                  /*required=*/false);
    (void)Resolve(g_prismLib, pPrism_registry_id_at,       "prism_registry_id_at",
                  /*required=*/false);

    // NULL config on purpose — see the PFN_prism_init declaration.
    g_prismCtx = pPrism_init(nullptr);
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
    CaptureSpokenW(text);

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

void SetSpeechCodepage(unsigned codepage) {
    if (codepage == 0) return;
    if (g_speechCodepage == codepage) return;
    g_speechCodepage = static_cast<UINT>(codepage);
    acclog::Write("Speech", "narrow-text codepage set to %u", codepage);
}

unsigned GetSpeechCodepage() { return g_speechCodepage; }

void Speak(const char* text, bool interrupt) {
    if (!g_normalReady || !text || !*text) return;
    acclog::Trace("Speech.spoke", "%s%s", interrupt ? "[!] " : "", text);
    CaptureSpoken(text);

    // Game codepage → UTF-8 via heap when needed. Chargen skill descriptions
    // are ~600+ narrow chars, so we size dynamically.
    char stack_buf[1024];
    char* buf = stack_buf;
    char* heap_buf = nullptr;
    // 4× worst-case expansion (a single-byte codepoint emits at most 3 UTF-8
    // bytes; we keep a small safety margin) + NUL.
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

// SpeakUrgent core. `applyVoice == true` means EnsureSapiVoice(voiceId) runs
// before the speak; the no-arg public overload skips it so the default 0-voice
// path doesn't pay for set_voice on every utterance.
static void SpeakUrgentImpl(const char* text, size_t voiceId, bool applyVoice) {
    if (!g_normalReady || !text || !*text) return;

    // Capture before the dispatch, not inside the rc==kPrismOk branch: the
    // one-shot suppress flag has to be consumed by exactly this call whether
    // or not the backend accepted the utterance, otherwise a dropped urgent
    // line would leave the flag armed and swallow the NEXT real capture.
    CaptureSpoken(text);

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

    if (applyVoice && g_sapiReady) {
        EnsureSapiVoice(voiceId);
    }

    PrismError rc = pPrism_backend_speak(target, speakText, /*interrupt=*/true);
    if (rc == kPrismOk) {
        // Log the original CP_ACP text so the patch log stays readable
        // in the same encoding as every other log line.
        acclog::Trace("Speech.spoke", "%s %s", tag, text);
    } else {
        // Do NOT fall back to the normal backend on a SAPI rc!=0. SAPI
        // dispatches asynchronously; by the time rc!=0 returns SAPI may
        // already be mid-utterance, and routing the same string to the
        // normal backend would produce double-speak. Drop the utterance;
        // the next SpeakUrgent call retries naturally.
        acclog::Trace("Speech",
                      "SpeakUrgent: prism_backend_speak rc=%d; "
                      "dropping utterance (no double-speak fallback)", rc);
    }
}

void SpeakUrgent(const char* text) {
    SpeakUrgentImpl(text, /*voiceId=*/0, /*applyVoice=*/false);
}

void SpeakUrgent(const char* text, size_t voiceId) {
    SpeakUrgentImpl(text, voiceId, /*applyVoice=*/true);
}

void SetUrgentVolumePercent(int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    g_sapiVolumeLoaded  = true;  // we are the authoritative value now
    g_sapiVolumePercent = percent;
    acc::settings::SetInt(kSapiVolumeKey, percent);  // persist across launches
    ApplySapiVolume();  // push to the live backend (no-op if not ready)
}

int GetUrgentVolumePercent() {
    EnsureSapiVolumeLoaded();
    return g_sapiVolumePercent;
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

const char* LastSpoken() { return s_lastSpoken; }

void SuppressNextCapture() { s_suppressNextCapture = true; }

void Shutdown() {
    // Deliberately do not free backends / shut down the context here —
    // Prism's bridge DLLs talk to RPC + COM objects, and tearing those down
    // inside DLL_PROCESS_DETACH risks loader-lock + COM-uninit ordering
    // issues. Process exit will reclaim everything.
    g_normalReady = false;
    g_sapiReady   = false;
}

}  // namespace prism
