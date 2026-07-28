# tutorial_hints.h (63 lines)

Public surface for the Endar Spire / global tutorial keyboard-hint
substitution. The on-screen text stays mouse-worded (unmodified) for a
sighted/rest-vision player; this module supplies a keyboard-oriented
replacement into the SPOKEN channel only, for two surfaces: silent
tutorial.2da popups (row read off CSWGuiTutorialBox +0x994) and
voice-acted Trask/pop dialogue lines (matched by TLK-resolved strref
against the rendered line). Pure poll — no engine hooks, no dialog.tlk
edits. See docs/llm-docs/tutorial-popup-mechanics.md.

## Declarations (in source order)

- L28 — `int ReadTutorialRow(void* tutorialBoxPanel)`
  note: CSWGuiTutorialBox +0x994, uint8, RE-confirmed; -1 on null/fault
- L32 — `const char* HintForTutorialRow(int row)`
  note: nullptr for rows with no mouse wording — leaves vanilla text
- L40 — `const char* HintForDialogLine(const char* renderedLine, uint32_t* outStrref = nullptr)`
  note: language-independent — matches via the engine's own TLK resolution, lazily built on first call
- L48 — `const char* HintForMouseText(const char* text)`
  note: matched on TEXT not panel identity, so it works before the popup registers in panels[]
- L53 — `bool IsSuppressedTutorialText(const char* text)` — convenience predicate over HintForMouseText
- L61 — `bool IsForcedSpokenDialogLine(const char* renderedLine)`
  note: tiny hand-curated set of VO-less subtitle lines that must bypass human-subtitle suppression (developer oversights)
