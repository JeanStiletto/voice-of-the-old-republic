# prism.h (49 lines)

Speech bridge — wraps the Prism C ABI. Two dispatch paths: Speak/Silence via normal backend (NVDA/JAWS/etc.), SpeakUrgent via SAPI to bypass NVDA's typed-character cancel. prism.dll loaded lazily via LoadLibrary — missing install degrades to silent mode. Init MUST NOT run inside DllMain.

## Declarations (in source order)

- L11 — `namespace prism`
- L14 — `bool Init()`
  note: idempotent; returns IsAvailable()
- L17 — `bool IsAvailable()`
- L20 — `void Speak(const wchar_t* text, bool interrupt)`
- L24 — `void Speak(const char* text, bool interrupt)`
  note: ANSI overload; converts via MultiByteToWideChar(CP_ACP)
- L35 — `void SpeakUrgent(const char* text)`
  note: routes through SAPI; interrupt implicitly true; falls back to normal channel on SAPI unavailability
- L36 — `void SpeakUrgent(const char* text, size_t voiceId)`
  note: per-utterance voice swap; voice 1 reserved for compass turn cues
- L39 — `void Silence()`
- L42 — `const wchar_t* ActiveScreenReader()`
  note: e.g. L"NVDA"; L"none" on init failure; stable for process lifetime
- L47 — `void Shutdown()`
  note: drops IsAvailable flag; Prism context not torn down (unsafe from DLL_PROCESS_DETACH)
