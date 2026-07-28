# menu_speak.h (18 lines)

Declares the single shared `SpeakChoice` helper — speak+log with a printf
context string, used by multiple per-menu "Speak*" paths that share this
exact shape (label speak with interrupt=true, log under the menu's tag).

## Declarations (in source order)

- L8 — `namespace acc::menu_speak`
- L14 — `void SpeakChoice(const char* tag, const char* label, const char* ctxFmt, ...)`
  note: log line format is `<tag> "speak <ctxFmt-result> [<label>]"` or `-> empty`
