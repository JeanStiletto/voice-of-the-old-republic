# cycle_input.cpp (1047 lines)

Implements Pillar 4 cycle keypress handling for both ingestion paths and the whole dash-family (`-`/Shift+-/Ctrl+-/Alt+-). `BindingsFor` centralises the per-category strings::Id + audio NavCue table (name/empty-message/cue), with Map-context Landmark renamed to "Map hint" to match the engine's own map-note terminology. `AnnounceCurrent` is the shared announce path used by every stepping handler: plays a 3D positional cue at the focused object, resolves its spoken name through a name-resolution cascade (shipped static hint → map pin note_text → waypoint map-note → GetObjectName → category-name fallback), appends a same-name disambiguating ordinal (listing-relative rank for Map context via `AppendPositionOrdinal`, or the shared `acc::narration::AppendDisambiguator` for World context so it matches what Q/E speaks), then stamps the unified `narrated_target` activation slot. `OnPathfindFocus` (Shift+-) is a toggle: cancels an in-flight approach on a second press, otherwise dispatches `UseObject` for real interactable targets or `WalkTo` straight to the coordinate for map pins/handle-less targets/Transition-category triggers (transitions are TRIGGER regions that fire on walk-IN, not on USE). `OnBeaconFocus` (Ctrl+-) computes an A* path over the mod's own static per-area nav graph (the engine refuses to plot for the leader) and arms a heartbeat beacon. `OnPathfindFocusForce` (Alt+-) is a diagnostic alternate using `ForceMoveToPoint` to discriminate queue-contention from input-mode failure modes; unsupported for map pins.

## Declarations (in source order)

- L42 — `namespace acc::cycle_input`
- L50 — `bool g_engineShiftHeld`
- L62 — `using acc::engine::ClockPosition`
- L67 — `struct CategoryBindings` (name/empty/cue)
- L81 — `acc::audio::NavCue RefineDoorCue(acc::audio::NavCue cue, void* obj)`
  note: resolves the DoorOpen placeholder against real open_state + material (metal/wood/stone)
- L92 — `CategoryBindings BindingsFor(acc::filter::CycleCategory c, acc::filter::CycleContext ctx = World)`
- L132 — `int FormatItemPayload(const char* name, bool haveYaw, int clock, int metres, char* outBuf, size_t outBufSize)`
- L147 — `void ResolvePinNoteText(void* pin, char* outBuf, size_t bufSize)`
- L160 — `void ResolveEntryName(const CategoryListing&, int idx, bool mapHint, char* out, size_t size)`
  note: mirrors AnnounceCurrent's name cascade so same-name numbering keys off exactly what the user hears
- L200 — `void AppendPositionOrdinal(const CategoryListing&, int focusedIndex, bool mapHint, char* name, size_t nameSize)`
  note: listing-relative rank (Map context only); World context uses acc::narration::AppendDisambiguator instead
- L242 — `void AnnounceCurrent(const CategoryListing& listing, const char* categoryPrefix, acc::filter::CycleContext ctx)`
  note: cue → name resolution cascade → disambiguator/state-label → clock/distance → stamp narrated_target → (Map) pan cursor
- L424 — `void OnCycleItem(bool prev, acc::filter::CycleContext ctx)`
- L435 — `void OnCycleEdge(bool last, acc::filter::CycleContext ctx)`
  note: Ctrl+,/Ctrl+. jump to first/last item
- L447 — `void OnCycleCategory(bool prev, acc::filter::CycleContext ctx)`
- L469 — `acc::filter::CycleCategory ClassifyForCycle(void* obj)`
- L488 — `struct NarratedActivation` (obj/handle/pos/name/category/isMapPin)
- L497 — `bool ResolveNarratedActivation(NarratedActivation& out)`
  note: always re-reads pos/name at use-time, not stamp-time — the object may have moved since it was announced
- L550 — `bool TryResolveOrAnnounceNoFocus(NarratedActivation& a, const char* logTag)`
- L564 — `void OnAnnounceFocus()` (`-`)
  note: reads the unified narrated_target slot, not cycle_state directly, so it repeats a passive-narrate target too
- L607 — `acc::strings::Id GuidancePreRollFor(acc::filter::CycleCategory c)`
- L646 — `void OnPathfindFocus()` (Shift+-)
  note: toggle-cancel first; Transition category forces WalkTo(coord) since transitions are trigger regions, not USE targets
- L767 — `void OnBeaconFocus()` (Ctrl+-)
  note: toggle-cancel; ComputePath over the mod's static nav graph, then StartBeacon
- L849 — `void OnPathfindFocusForce()` (Alt+-)
  note: unsupported for map pins (no ForceMoveToPoint analog); logs "(force path)" vs "(queue path)" for post-mortem diff
- L898 — `bool TryHandleEvent(int param_1, int param_2)`
  note: tracks engine-side shift latch; filters to comma/period/announce codes; gates on GetPlayerPosition; claims rising edge to prevent PollWin32 double-dispatch
- L984 — `void PollWin32()`
  note: precedence Ctrl > Alt > Shift > bare for the dash family, encoded via mutually-exclusive Action modifier masks
