# radial_menu.h (39 lines)

Radial action menu input gate, state, and speech. Radial is not a top-level
CSWGuiPanel but embedded in CSWGuiMainInterface; active flag is set by
acc::picker::Drive after successful PopulateMenus. Self-dismisses when all
action_lists[].size == 0.

## Declarations (in source order)

- L19 — `namespace acc::radial_menu`
- L22 — `bool ArmAfterPopulate(const char* targetName)`
  note: returns true iff at least one row was populated.
- L24 — `bool IsActive()`
- L27 — `bool HandleInputEvent(int code, int value)`
  note: press-edge only; release events of consumed keys still pass through.
- L29 — `void Tick()`
- L32 — `void ForceDisarm(const char* reason)`
  note: does not clean engine state; engine refreshes on next click / PopulateMenus.
- L37 — `void ScheduleWideDiag(int frames, const char* tag)`
  note: schedules N ticks of wide TAM diagnostic dumps; frames clamped [1..10]; SEH-safe reads only.
