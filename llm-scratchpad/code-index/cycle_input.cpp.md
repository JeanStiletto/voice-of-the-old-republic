# cycle_input.cpp (851 lines)

Implementation of the Pillar 4 cycle input dispatch. Handles `,`/`.`/`-` keys (item step, category step, announce/autowalk/beacon) for both the engine event path and the Win32 poll path. Includes clock-position formatting, category-to-bindings mapping, and all five dash-family actions.

## Declarations (in source order)

- L36 — `namespace acc::cycle_input`
- L38 — `namespace { // anonymous`
- L55 — `int ClockPosition(float playerYawDeg, float dx, float dy)`
  note: returns 1..12 clock-face position (12 = directly ahead) of an object relative to the player's yaw
- L69 — `struct CategoryBindings`
  note: maps a CycleCategory to its speech string IDs and 3D nav cue; used by BindingsFor to centralise the dispatch table
- L83 — `acc::audio::NavCue RefineDoorCue(acc::audio::NavCue cue, void* obj)`
  note: converts the DoorOpen placeholder cue to the correct material-specific closed-door cue; pass-through for any other NavCue
- L94 — `CategoryBindings BindingsFor(acc::filter::CycleCategory c, acc::filter::CycleContext ctx)`
- L134 — `int FormatItemPayload(const char* name, bool haveYaw, int clock, int metres, char* outBuf, size_t outBufSize)`
  note: formats the "{name}, {clock} o'clock, {metres} metres" (or no-clock variant) TTS payload into outBuf
- L154 — `void AnnounceCurrent(const acc::cycle::CategoryListing& listing, const char* categoryPrefix, acc::filter::CycleContext ctx)`
  note: speaks the focused item with 3D cue + clock + distance; stamps the narrated_target slot; pans map cursor in Map context
- L289 — `void OnCycleItem(bool prev, acc::filter::CycleContext ctx)`
  note: steps within current category and announces the new focus
- L301 — `void OnCycleCategory(bool prev, acc::filter::CycleContext ctx)`
  note: skips to next/prev non-empty category; speaks "{Category}. {item}" prefix
- L323 — `acc::filter::CycleCategory ClassifyForCycle(void* obj)`
  note: returns Count_ when obj falls outside the six nav categories (should not occur in practice)
- L342 — `struct NarratedActivation`
  note: bundle resolved from the narrated_target slot for the dash-family handlers; all fields zeroed on empty/stale slot
- L351 — `bool ResolveNarratedActivation(NarratedActivation& out)`
  note: re-reads live pos/name at activation time, not stamp time; returns false when slot empty or stale
- L450 — `acc::strings::Id GuidancePreRollFor(acc::filter::CycleCategory c)`
  note: returns the per-kind pre-roll string ID ("Sprich mit X", "Öffne X", "Hebe X auf") matching the interact_hotkey vocabulary
- L484 — `void OnPathfindFocus()`
  note: Shift+- handler; toggle-cancels an in-flight UseObject autowalk on second press; map pins redirect to Ctrl+-
- L586 — `void OnBeaconFocus()`
  note: Ctrl+- handler; starts A* beacon or cancels if already active
- L674 — `void OnPathfindFocusForce()`
  note: Alt+- diagnostic handler; uses ForceMoveToPoint instead of AddMoveToPointAction to isolate queue-contention failures
- L727 — `} // namespace (anonymous)`
- L729 — `bool TryHandleEvent(int param_1, int param_2)`
- L791 — `void PollWin32()`
