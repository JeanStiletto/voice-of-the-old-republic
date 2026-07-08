# Custom on-demand tutorial popups — engine mechanics (RE reference)

How the accessibility mod drives the engine's **own** tutorial popup with custom
text, on demand, so it pauses/dismisses exactly like a stock game tutorial.
Implemented in `patches/Accessibility/tutorial_popup.cpp`; this doc consolidates
the RE so future UI/popup work doesn't have to re-derive it. Addresses are
GoG/Steam-identical.

## Two delivery surfaces (both shipped, DE+EN)
- **Surface 1 — silent `tutorial.2da` popups (game-wide).** The engine already
  shows these; we only substitute the *spoken* text. Keyed by the popup's source
  row at `CSWGuiTutorialBox +0x994`. See `tutorial_hints.cpp` (`HintForTutorialRow`,
  `kTutorialMouseMsgs`). On-screen text left as vanilla mouse wording.
- **Surface 2 — voice-acted Trask / `end_pop*` lines (Endar Spire).** Suppressed
  during Trask's Basic VO; at the dialogue reply break we *fire our own* tutorial
  popup carrying the keyboard hint. See `tutorial_popup.cpp`.

## Firing a tutorial popup with custom text (Surface 2 recipe)
1. `gui = acc::engine::ResolveGuiInGame()` (mod helper). `CGuiInGame` members:
   `tutorial_box` @+0xa0, `message_box` @+0x98, `field45_0xb4` @+0xb4.
2. **Bypass the once-only gate.** It's a bitfield: byte at `CGuiInGame + 0xba8 +
   (reason>>3)`, bit `(reason & 7)`. Clear it before mounting so it fires every
   time. (Ghidra mislabels this region as `field127_0xb58` = `CExoString[8]`;
   the shown-bits live just past it at +0xba8.)
3. **Mount:** `CGuiInGame::ShowTutorialWindow` @0x0062f4a0, `__thiscall(gui, int
   reason, u32, u32, u32)`. Call this **directly** — it does NOT check
   `field45_0xb4`, so it mounts even mid-dialogue. (The public funnel
   `CClientExoApp::ShowTutorialWindow` @0x005edf40 → internal @0x005f4120 →
   `TutorialReasonWillShowWindow` @0x0062f420 DOES check field45 and refuses
   during dialogue — avoid it here.) The direct call does NOT set the reason/
   message; do that yourself.
4. **Configure as a real tutorial** (single Weiter/OK prompt, not a generic
   two-button message box): `CSWGuiTutorialBox::SetTutorialReason` @0x006aa900,
   `__thiscall(box, int reason)`. Without this the box renders OK+Abbrechen.
5. **Set visible text:** `CSWGuiMessageBox::SetMessage(strref)` @0x006249d0,
   `__thiscall(box, u32 strref)` — resolves via the TLK and sets the text.
   SAFE. The CExoString overload @0x006271a0 takes the string **by value and
   destroys it** (would free a caller buffer → crash) — do NOT use it.
6. **Pause** like a tutorial: `CServerExoApp::SetPauseState` @0x004ae9a0
   `__thiscall(server, int sourceBit=2, u32 on)`; server = `*(*(0x007A39FC))
   + 0x08`. Unpause on dismiss + `CExoSoundInternal::SetSoundMode` @0x005d5e80
   `(exo=*0x007a39ec, 0)` to un-mute. (Mirror of `engine_subscreen.cpp`.)
7. **Dismiss detection:** poll the GUI manager panels[] for a `TutorialBox`; when
   it's gone, the user pressed Weiter — clear state + unpause.

Reason value used: `0x2a` (42 = Movement_Keys, last row) — shown once at game
start, never again, and NOT in the keyboard-hint row map, so nothing collides.

## Trigger (Surface 2): when to fire
Fire at the dialogue **reply break**, specifically when a reply first becomes
*readable/navigable* — `MonitorDialogReplies` reaches its `src=dialog-state`
branch — which means the entry's VO has ended and the player can choose. Do NOT
fire on "reply list present" (that's true at VO start → talks over Trask and
steals Enter-as-skip), and do NOT rely on a `sel −1 → 0` transition: the reply
listbox is ONE persistent object whose selection never resets to −1 between
prompts. Trask delivers several rewritten lines back-to-back before a break, so
`dialog_speech` records each (`HintForDialogLine` → hint + strref) and
`tutorial_popup` **accumulates** them (newline-joined) into one popup.

## The 5 UI-announce paths (must ALL be gated for a managed TutorialBox)
A single popup's text is reachable by five independent speakers with no shared
"say once" authority. Any managed TutorialBox must be handled in every one, or
the mouse text leaks:
1. **Content-fingerprint monitor** (`menus_monitors::MonitorPanelContents`) —
   first-sight body speech. Override → hint.
2. **Single-row listbox monitor** (`menus.cpp::OnListBoxSetActiveControl`) —
   suppress (Surface-1 by mouse-strref text match / Surface-2 by `SyntheticActive`).
3. **Engine focus-announce** (`menus.cpp::AnnounceNewFocusedControl`) — suppress
   for the managed TutorialBox (stops a stray "Abbrechen" on open).
4. **Arrow-nav chain** (`menus_monitors::AnnounceControl`) — substitute the hint
   for the **message listbox** (identity, not text); buttons read their labels.
5. **Per-tick focused-control re-announce** (`menus_monitors::MonitorFocusedControl`)
   — suppress the synthetic message row (AnnounceControl already speaks it).
This is the "unify the announce paths" tech-debt (see `docs/known-issues.md`):
the clean end-state is one chokepoint keyed by identity, not per-path matching.

## Language independence
Match/resolve everything through the engine's own TLK — `acc::engine::LookupTlk`
(`CTlkTable::GetSimpleString` @0x0041e8f0, table ptr @0x007a3a08) — so mouse-text
suppression and dialogue-line detection work in the user's language without
per-language text tables. The keyboard-hint *content* is authored in the mod's
`acc::strings` table (DE+EN done; FR/IT/ES fall back to the vanilla mouse popups).
