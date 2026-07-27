# tutorial_hints.cpp (293 lines)

Implements the keyboard-hint substitution tables. Three lazily-built,
TLK-resolved lookup tables (built once real TLK data is available, retried
on later calls until then): dialogue-line hints keyed by source strref
(Trask/pop Endar Spire lines + a Taris Carth stealth line), popup
mouse-message text keyed by strref (covers every mapped tutorial.2da row,
including multi-page rows like Attack/Attack_Button_Mash), and a tiny
forced-spoken set for VO-less subtitle lines that would otherwise be
silenced by human-subtitle suppression. All text matching uses trimmed
(whitespace-insensitive) comparison since the engine's own rendered line
may carry stray padding vs its TLK resolution. Talks to: engine_reads
(LookupTlk), strings (Get(Id)), log.

## Declarations (in source order)

- L21 — `kTutorialBoxRowOffset = 0x994` — CSWGuiTutorialBox row byte
- L26-29 — `struct DialogHint { uint32_t strref; Id id; }`
- L30-62 — `kDialogHints[]` — strref->hint Id table (Trask intro/equip/camera/
  footlocker/menus/action-menu/medkit/door/security/heal/level-up + Taris stealth)
  note: strref 10326 hangs the core-controls popup off the stock story-VO line closing Trask's intro tree, not a rewritten mouse line
- L67-73 — `struct ResolvedHint` + `s_resolved[]` cache + `s_resolvedBuilt` guard
- L78 — `bool EqualsTrimmed(a, b)` — whitespace-insensitive compare
- L89 — `void BuildResolvedIfNeeded()` — lazy TLK resolution, retries until >=1 resolves
- L114 — `int ReadTutorialRow(panel)` — SEH-guarded +0x994 byte read
- L124 — `const char* HintForTutorialRow(row)` — switch over ~18 mapped rows
- L159-187 — `struct MouseMsg` + `kTutorialMouseMsgs[]` — per-row message strref -> hint Id
  (covers Attack/Attack_Button_Mash's two message pages each)
- L192 — `void BuildMouseTextIfNeeded()`
- L209 — `const char* HintForMouseText(text)`
- L222 — `bool IsSuppressedTutorialText(text)`
- L239-241 — `kForcedSpokenStrrefs[] = {39454}` — Trask's level-up-gate line (verified German TLK, no VO)
- L246 — `void BuildForcedTextIfNeeded()`
- L263 — `const char* HintForDialogLine(renderedLine, outStrref)`
- L281 — `bool IsForcedSpokenDialogLine(renderedLine)`
