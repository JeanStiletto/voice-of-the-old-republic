# prism.h (74 lines)

Public speech-bridge surface. Warns that `Init()` must not run inside
`DllMain` — Prism loads COM + screen-reader driver DLLs, both unsafe under
the loader lock.

## Declarations (in source order)

- L14 — `bool Init()`
- L17 — `bool IsAvailable()`
- L20 — `void Speak(const wchar_t* text, bool interrupt)`
- L24 — `void Speak(const char* text, bool interrupt)`
  note: right entry point for CExoString-sourced engine text; converts via GetSpeechCodepage().
- L36 — `void SetSpeechCodepage(unsigned codepage)`
  note: must be pinned explicitly for languages whose codepage differs from the OS codepage (e.g. Russian 1251 on a German/English Windows); safe to call before Init().
- L37 — `unsigned GetSpeechCodepage()`
- L48-49 — `void SpeakUrgent(const char* text); void SpeakUrgent(const char* text, size_t voiceId)`
  note: routes through SAPI to bypass NVDA's typed-character cancel; falls back to normal channel if SAPI unavailable; interrupt is implicitly true.
- L59 — `void SetUrgentVolumePercent(int percent)`
  note: persists across launches via acc_settings.ini; only affects the SAPI urgent channel.
- L60 — `int GetUrgentVolumePercent()`
- L63 — `void Silence()`
- L66 — `const wchar_t* ActiveScreenReader()`
- L71 — `void Shutdown()`
  note: drops IsAvailable() flag only; Prism context persists until process exit.
