# interact_hotkey.cpp (1104 lines)

Enter / Shift+Enter interact dispatch, plus the Win32 per-tick poller that
also routes every other in-world/menu-adjacent action (action-bar submenu
opens, level-up, bare 1-7 announce, examine view, combat queue, unified
action menu nav, bare-R native-default narration). `DispatchInteractImpl` is
the shared post-resolution flow (classify → engine action picker → speak →
UseObject/click-pipeline fallback), exposed publicly as `DispatchInteract` so
`view_mode` can drive the same pipeline from its own hover-target channel.
Resolves the interact target exclusively from `narrated_target` (no fallback
to engine LastTarget — unheard targets must not be actable). Talks to
`engine_picker` (action snapshot/drive), `unified_action_menu`,
`guidance_approach`/`guidance_autowalk` (walk-to-act tracking),
`combat_queue`, `examine_view`, `engine_levelup`. Gotcha: transitions dispatch
via `WalkTo(coord)` instead of the picker (trigger regions fire on walk-in,
not "use"); never short-circuit a locked door's open attempt — the failed
open is often the story trigger (see the WARNING comment at L206).

## Declarations (in source order)

- L50 — `void ArmInteractApproach(const char* name, void* target, bool inputDisabled, bool isDialog)` — arms guidance_approach's unified walk-to-act watchdog
- L76 — `bool ShouldSwitchFromInGameMenu()` — true iff only the in-game menu strip blocks (not a modal/dialog/store), enabling menu-hotkey-style switching
- L84 — `acc::strings::Id PreRollFor(acc::filter::CycleCategory c)` — per-kind pre-roll verb (open/talk/take)
- L103 — `acc::filter::CycleCategory ClassifyForInteract(void* obj)` — first-matching-category classify, mirrors passive_narrate
- L129 — `void* ResolveInteractTarget(uint32_t* outHandle)` — reads narrated_target only, no engine-LastTarget fallback
- L150 — `void DispatchInteractImpl(void* target, uint32_t handle, bool forceRadial)` — forward decl (defined L192)
- L157 — `void OnInteract(bool forceRadial)` — map-pin redirect hint, no-target fallback speech, then dispatch
- L192 — `void DispatchInteractImpl(...)`
  note: order is name resolve → classify → transition WalkTo(coord) special-case → picker populate-only (radial or default-action label) → speak → dispatch (use-verb 0x3f7 via UseObject, talk 0x3ea via ActionInitiateDialog with input left enabled, else engine click pipeline) → UseObject fallback → failure speech. Never suppress a locked-door open attempt (WARNING at L206-224).
- L513 — `void AnnounceBarePersonalKey(int slot)` — "X eingesetzt"/empty-column speech for bare 4-7; gated on combat_queue's pre-press depth to avoid phantom announces on consumed Shift-combos
- L598 — `void AnnounceBareTargetKey(int row)` — same pattern for bare 1-3 target-action rows
- L648 — `void DispatchInteract(void* target, uint32_t handle, bool forceRadial)` — public forwarder (lay-off 5 seam) for view_mode
- L652 — `void PollHotkey()`
  note: reads every Action rising edge from the registry up front; routes (in priority order) action-bar/target-row Shift+N openers, Shift+L level-up, bare 1-7 announce, examine_view input, combat_queue input, unified_action_menu input, bare-R native-default narration, then Enter/Shift+Enter interact dispatch (deferring to view_mode when it owns the press, swallowing the editbox-submit latch's coincident Enter)
