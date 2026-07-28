# guidance_approach.h (90 lines)

Header for the unified approach tracker — replaces three previously separate
observers (autowalk's in-flight tracker, interact_hotkey's dialog/use
watchdog, cycle_input's block-watch) that all keyed on the same movement
signal. One arm, one tick, one threshold set.

## Declarations (in source order)

- L36 — `namespace acc::guidance`
- L42 — `enum class ApproachOwner { Cycle, Interact }`
  note: only Cycle-owned walks are toggle-cancellable by a second Shift+- press.
- L44 — `struct ApproachArm` — owner/name/targetObj/targetPos/inputDisabled/isDialog/speakBlocked
- L56 — `void ArmApproach(const ApproachArm& arm)`
- L59 — `void TickApproach()`
- L64 — `bool IsApproachInFlight()` — Cycle-owned only
- L70 — `bool IsAnyModApproachInFlight()` — any owner
- L78 — `bool CancelByMovement()`
- L82 — `void CancelApproach()` — silent teardown for explicit cancel paths
- L88 — `void SpeakWayBlocked(const char* name, const Vector& targetPos)`
