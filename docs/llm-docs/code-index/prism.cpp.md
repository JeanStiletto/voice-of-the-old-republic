# prism.cpp (480 lines)

Prism speech bridge implementation. Resolves all Prism exports via GetProcAddress at Init time. Maintains two backends: normal (acquire_best) and SAPI (explicit acquire + rate set to kPrismSapiUrgentRate=0.8). Handles CP_ACP→UTF-8 re-encoding for KOTOR CExoString payloads. Caches SAPI voice count and current voice id to avoid redundant set_voice calls.

## Declarations (in source order)

- L26 — `namespace prism`
- L50 — `typedef PrismConfig (__cdecl* PFN_prism_config_init)(void)` (anonymous namespace, plus ~12 more PFN typedefs)
- L101 — `template <typename T> bool Resolve(HMODULE lib, T& fn, const char* name, bool required = true)` (anonymous namespace)
- L118 — `static bool ReencodeAcpToUtf8(const char* in, char* outBuf, size_t outBufSize)` (anonymous namespace)
  note: CP_ACP→UTF-8 via WideCharToMultiByte; stack buffer with heap fallback
- L147 — `static bool WideToUtf8(const wchar_t* in, char* outBuf, size_t outBufSize)` (anonymous namespace)
- L159 — `static bool TryAcquireSapi()` (anonymous namespace)
- L219 — `static bool TryAcquireNormal()` (anonymous namespace)
- L235 — `static bool EnsureSapiVoice(size_t voiceId)` (anonymous namespace)
  note: cached; no-ops when voiceId==g_sapiVoiceCurrent; range-checked against g_sapiVoiceCount
- L252 — `bool Init()`
- L335 — `bool IsAvailable()`
- L343 — `void Speak(const wchar_t* text, bool interrupt)`
- L377 — `void Speak(const char* text, bool interrupt)`
- L408 — `static void SpeakUrgentImpl(const char* text, size_t voiceId, bool applyVoice)` (anonymous namespace)
  note: prefers SAPI; does NOT fall back on rc!=0 (avoids double-speak from async SAPI)
- L451 — `void SpeakUrgent(const char* text)`
- L455 — `void SpeakUrgent(const char* text, size_t voiceId)`
- L459 — `void Silence()`
- L466 — `const wchar_t* ActiveScreenReader()`
- L471 — `void Shutdown()`
