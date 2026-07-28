# engine_picker.h (143 lines)

Engine action picker — drives the game's context-sensitive action
dispatcher for an arbitrary target without moving the cursor. Documents the
4-step mechanism (SetMainInterfaceTarget, GetDefaultActions, click-gate
write, HandleMouseClickInWorld) and the CSWGuiInterfaceAction layout at
+0x4c8. Talks to engine_area, engine_player (input-disable), engine_radial
(diagnostics + ReanchorRadial's populate chain).

## Declarations (in source order)

- L38 — `namespace acc::picker`
- L52 — `inline bool IsWalkToActVerb(uint32_t action_id)`
  note: talk(0x3ea)/door(0x3f2)/mine(0x3f4)/bash(0x3f5)/use-open(0x3f7) — composite walk-then-act verbs; Drive() leaves input ENABLED for these so the engine's native server-side approach isn't suppressed (fixes the distant-talk/distant-loot freeze). 0x404 noop deliberately excluded.
- L66 — `struct ActionSnapshot`
  note: radial_opened true means count==0 or forceRadial caused PopulateMenus to open the radial instead of dispatching.
- L104 — `bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot, bool forceRadial = false, bool populateOnly = false)`
  note: forceRadial bypasses default-action dispatch (Shift+Enter). populateOnly runs only the read half (SetMainInterfaceTarget + GetDefaultActions + snapshot + radial fallback) and skips the click-gate + HandleMouseClickInWorld dispatch, letting the caller choose the dispatch primitive per verb.
- L118 — `bool ReanchorRadial(uint32_t targetServerHandle)`
  note: re-runs Drive's force-radial setup (SetMainInterfaceTarget + GetDefaultActions + PopulateMenus) with no diagnostics, so the radial input handler can call it every keypress to override cursor drift re-pointing the target-action menu.
- L123 — `bool ReadCurrent(ActionSnapshot* outSnapshot)`
  note: reads +0x4c8 without driving anything — observes the engine's own picker (cursor-hover / passive-selection) for diagnostics.
- L141 — `bool InitiateDialog(uint32_t targetServerHandle)`
  note: calls CSWCCreature::ActionInitiateDialog @0x0060f620 directly on the target NPC, bypassing HandleMouseClickInWorld's first-click-vs-confirm gate (which otherwise needs two Enter presses to talk); leaves player input enabled — the engine walks-then-talks server-side.
