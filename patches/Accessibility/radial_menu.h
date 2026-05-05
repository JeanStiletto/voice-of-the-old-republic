// Radial action menu — input gate, state, and speech wiring.
//
// Layer: input/menu (consumes engine_radial primitives + speech). Mirrors
// the equip-picker pattern in menus.cpp: the radial isn't a top-level
// CSWGuiPanel (it lives embedded inside CSWGuiMainInterface, see
// engine_radial.h), so the panel-kind-driven dispatch in OnHandleInputEvent
// has nothing to bind to. We track our own "active" flag — set when
// `acc::picker::Drive` falls into the PopulateMenus branch — and short-
// circuit the manager's input dispatch while it's set.
//
// User contract while active:
//   Up / Down  — switch which row (0..2 = combat / general / examine etc.,
//                naming follows whatever PopulateMenus filled). Skips rows
//                with action_lists[r].size == 0. Speaks the new row's
//                currently-visible action label.
//   Left/Right — cycle the action *within* the current row via the engine's
//                CSWGuiTargetActionMenu::SelectPrevAction / SelectNextAction.
//                Speaks the new label.
//   Enter      — dispatch the current row's selected action via
//                CSWGuiTargetActionMenu::DoTargetAction(row). The engine's
//                normal action pipeline runs (walk-to + open / talk / use)
//                and we drop our active flag.
//   Esc        — drop our active flag without dispatching. The engine-side
//                radial state (action_lists / target_actions) is left in
//                place; the next click or PopulateMenus call refreshes it.
//
// Self-dismiss: the per-tick Tick() reads action_lists[0..2].size from the
// engine. When all three rows have count == 0 (engine cleared the menu —
// e.g. dispatch completed, target lost), we drop active state silently.
// First implementation; if the engine retains stale rows we'll switch to a
// stricter staleness signal (target id, target_type byte).

#pragma once

namespace acc::radial_menu {

// Called from `acc::picker::Drive` after a successful PopulateMenus. Reads
// the engine's current row counts, picks the first non-empty row as the
// initial focus, speaks the opener pre-roll ("Aktionsmenü, X. Aktion 1
// von N: <label>") and arms the input gate. `targetName` is the localised
// name of the radial's target (already resolved by the caller from
// CSWSObject::tag / first_name).
//
// No-op when the engine has no populated rows (count == 0 across all 3) —
// in that case the radial isn't actually navigable and we leave the gate
// disarmed so the user falls back to the existing per-kind pre-roll.
//
// Returns true when the gate was armed (i.e. the menu has at least one
// usable row); false when nothing was populated.
bool ArmAfterPopulate(const char* targetName);

// True when our gate is armed. Callers (interact_hotkey, future cycle
// blockers) check this to skip in-world handlers.
bool IsActive();

// Manager-level input gate. Called from menus.cpp's OnHandleInputEvent
// BEFORE the per-panel-kind switch. When IsActive() returns the result of
// our handling for `code` / `value`; otherwise returns false (pass-through).
//
// `code` is a pre-translation logical InputIndices value (kInputNavUp,
// kInputEnter1, …). `value` mirrors the engine's val= field — non-zero is
// a press edge, zero is release.
//
// Press-edge only — release events for our consumed keys still pass
// through cleanly (no swallowing of the matching key-up).
bool HandleInputEvent(int code, int value);

// Per-tick verifier. Called from OnUpdate. Re-resolves the engine TAM,
// reads action_lists[].size; if all rows are empty the engine has cleared
// the menu and we drop our active flag. Cheap (one chain walk + 3 reads).
void Tick();

// Forced disarm. Called when the user navigates away from the in-world
// context (panel push, area transition). No engine-side cleanup — first
// implementation accepts that the engine's data may persist until the
// next click; user gets keyboard control back, which is the priority.
void ForceDisarm(const char* reason);

// Schedule wide TAM diagnostic dumps for the next `frames` ticks. Tag
// included in each dump line. Used to observe whether the engine
// asynchronously fills target_action_menu state across frames after a
// PopulateMenus call. `frames` clamped to [1..10]. tag may be null.
//
// Crash-safety: this only schedules reads, not writes. Reads are
// SEH-wrapped per acc::engine_radial helpers.
void ScheduleWideDiag(int frames, const char* tag);

// Crash-safety gate for a one-shot inner CSWGuiTargetActionMenu::PopulateMenus
// call. Returns true if a populate has NOT been issued in the last
// `cooldownFrames` ticks; caller should record the populate by calling
// MarkPopulateIssued on success. Used by engine_picker to ensure we
// never re-populate the same TAM in back-to-back frames (the triple-
// populate test crashed the engine's Draw on next frame —
// patch-20260505-073407.log).
bool CanIssueInnerPopulate(int cooldownFrames);
void MarkPopulateIssued();

}  // namespace acc::radial_menu
