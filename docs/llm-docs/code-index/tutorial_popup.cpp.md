# tutorial_popup.cpp (234 lines)

Implements the synthetic tutorial popup. Calls
`CGuiInGame::ShowTutorialWindow` directly (bypasses the funnel's "no
tutorials during dialogue" gate, since it doesn't check field45_0xb4),
clears the tutorial once-shown bitfield for a repurposed fixed reason
(0x2a = row 42 = Movement_Keys, chosen because it's shown once at game
start and never again naturally), configures the box as a real tutorial
via `SetTutorialReason`, and sets its VISIBLE text to the line's original
strref via `SetMessage(strref)` while the SPOKEN text is routed
separately through `SyntheticHint()`. Issues its own pause
(`SetPauseState` + audio unmute on release) like other modal popups.
Talks to: engine_manager (GUI manager panel stack), engine_panels
(ResolveGuiInGame, IdentifyPanel), log.

## Declarations (in source order)

- L20-34 — engine addresses: ShowTutorialWindow@0x0062f4a0, SetMessage(strref)@0x006249d0
  (NOT the CExoString overload, which destroys its arg), SetTutorialReason@0x006aa900
- L36-51 — CGuiInGame.tutorial_box offset(+0xa0), row byte(+0x994), once-shown
  bitfield base(+0xba8), `kSyntheticReason = 0x2a`
- L54-62 — pause primitives: SetPauseState@0x004ae9a0, SetSoundMode@0x005d5e80 (unmute on release)
- L69-75 — module state: `s_pendingHints[4096]` (accumulator), `s_pendingStrref`,
  `s_active`/`s_activeHintBuf`/`s_activeHint`, `s_paused`
- L77 — `void SetPause(bool on)` — SEH-guarded; unmutes audio on release
- L101 — `bool TutorialBoxPresent()` — scans GUI manager panel stack (first 16) for TutorialBox
- L117 — `void FirePopup(strref, hint)` — sets s_active BEFORE mounting (so suppression
  gates key on it), clears once-shown bit, mounts + configures the box, pauses
- L168 — `bool PendingContainsHint(hint)` — de-dupes adjacent lines sharing one hint
- L181 — `void RecordPendingHint(strref, hint)` — newline-accumulates; first line's strref drives visible text
- L201 — `bool FirePendingAtReplyBreak()` — moves accumulator to active buffer, calls FirePopup
- L217-218 — `bool SyntheticActive()` / `const char* SyntheticHint()`
- L220 — `void PollDismiss()` — detects TutorialBoxPresent() going false, clears state, unpauses
