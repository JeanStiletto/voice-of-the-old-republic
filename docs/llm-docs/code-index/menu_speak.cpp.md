# menu_speak.cpp (27 lines)

Tiny shared helper implementing `SpeakChoice`: formats a printf-style context
string, speaks `label` (interrupt=true) via `prism::Speak`, and logs under a
caller-supplied tag. Used by several per-menu "Speak*" call sites
(unified_action_menu, examine_view, combat_queue) that all shared the same
speak+log shape before being consolidated here.

## Declarations (in source order)

- L9 — `namespace acc::menu_speak`
- L11 — `void SpeakChoice(const char* tag, const char* label, const char* ctxFmt, ...)`
  note: empty/null label logs "-> empty" and is not spoken
