# prism.cpp (665 lines)

Speech bridge to the vendored Prism library, loaded via `LoadLibrary` +
`GetProcAddress` (never statically linked) so a missing `prism.dll` degrades
to silent mode. Two channels: "normal" (`Speak`/`Silence`), which uses
`prism_registry_acquire_best` to pick whatever screen-reader backend Prism
ranks highest (NVDA/JAWS/ZDSR/OneCore/SAPI...); and "urgent"
(`SpeakUrgent`), which explicitly acquires the SAPI backend to bypass NVDA's
typed-character cancel (used for map-cursor/compass/walking cues). All
backend-acquire/initialize calls are SEH-guarded as a belt-and-suspenders net
against a vendor screen-reader DLL delay-load fault (0xC06D007E/7F) — the
primary fix lives upstream in prism's `backend_registry.cpp`. Re-encodes
narrow text from the game's codepage (`CP_ACP` by default, pinned per-language
via `SetSpeechCodepage`) to UTF-8 before dispatch, since Prism's SAPI backend
strict-validates UTF-8. Persists the SAPI urgent-volume slider via
`mod_settings_store`.

## Declarations (in source order)

- L34-49 — Prism C-ABI mirror: `struct PrismConfig`, `PrismContext`, `PrismBackend`, `PrismBackendId`, `PrismError`; `kPrismOk=0, kPrismErrAlreadyInitialized=15, kPrismErrUnknown=17, kPrismBackendIdSapi`; delay-load fault codes `kVcppDelayLoadModNotFound=0xC06D007E, kVcppDelayLoadProcNotFound=0xC06D007F`
  note: declared locally (not #include prism.h) so the patch DLL has no build dependency on the upstream tree.
- L56 — `constexpr float kPrismSapiUrgentRate = 0.8f`
  note: ~SAPI +6, distinctly faster than default so continuous WASD-panning cues don't drag.
- L58-71 — function-pointer typedefs `PFN_prism_*` for the full resolved ABI subset.
- L73-99 — globals: `g_prismLib, g_prismCtx, g_prismNormal, g_prismSapi, g_initTried, g_normalReady, g_sapiReady, g_sapiVoiceCurrent, g_sapiVoiceCount, g_sapiVolumePercent, g_sapiVolumeLoaded`
- L101 — `void EnsureSapiVolumeLoaded()`
- L113 — `wchar_t g_activeNameW[64]`
- L135 — `void ApplySapiVolume()`
  note: does not gate on g_sapiReady so it can run mid-acquire.
- L151 — `template<T> bool Resolve(HMODULE lib, T& fn, const char* name, bool required=true)`
- L177 — `bool ReencodeAcpToUtf8(const char* in, char* outBuf, size_t outBufSize)`
  note: guards Prism's SAPI PRISM_ERROR_INVALID_UTF8 rejection of any non-ASCII lead byte.
- L207 — `bool WideToUtf8(const wchar_t* in, char* outBuf, size_t outBufSize)`
- L240 — `const char* SehCodeName(DWORD code)`
- L251 — `PrismBackend* SehAcquireBest(PrismContext* ctx, DWORD* outCode)`
- L262 — `PrismBackend* SehAcquire(PrismContext* ctx, PrismBackendId id, DWORD* outCode)`
- L274 — `PrismError SehInitialize(PrismBackend* be, DWORD* outCode)`
  note: __try functions hold only PODs/raw pointers — MSVC forbids __try where C++ object unwinding is needed, so logging stays in callers.
- L286 — `bool TryAcquireSapi()`
  note: explicit acquire+initialize (unlike acquire_best); sets rate, applies persisted volume, caches voice count.
- L374 — `bool TryAcquireNormal()`
  note: single acquire_best call — deliberately does not reimplement any backend priority walk; prism owns that.
- L394 — `bool EnsureSapiVoice(size_t voiceId)`
  note: cached so repeated calls with the same id are free; out-of-range silently no-ops.
- L411 — `bool Init()`
  note: idempotent; sets DLL search dir to patch dir so prism.dll's delay-loaded bridges resolve against bundled neighbours.
- L496 — `bool IsAvailable()`
- L504 — `void Speak(const wchar_t* text, bool interrupt)`
  note: logs every call to the patch log (Speech.spoke, "[!]" prefix for interrupt) as the canonical speech audit trail.
- L538 — `void SetSpeechCodepage(unsigned codepage)`
- L545 — `unsigned GetSpeechCodepage()`
- L547 — `void Speak(const char* text, bool interrupt)`
  note: sizes the UTF-8 buffer dynamically (4x worst-case expansion) since chargen skill descriptions run 600+ narrow chars.
- L578 — `static void SpeakUrgentImpl(const char* text, size_t voiceId, bool applyVoice)`
  note: on SAPI rc!=0 deliberately does NOT fall back to normal backend — SAPI dispatches async and a fallback risks double-speak.
- L621 — `void SpeakUrgent(const char* text)`
- L625 — `void SpeakUrgent(const char* text, size_t voiceId)`
- L629 — `void SetUrgentVolumePercent(int percent)`
- L638 — `int GetUrgentVolumePercent()`
- L643 — `void Silence()`
- L650 — `const wchar_t* ActiveScreenReader()`
- L655 — `void Shutdown()`
  note: deliberately does not tear down Prism context/backends — COM/RPC teardown inside DLL_PROCESS_DETACH risks loader-lock issues; process exit reclaims everything.
