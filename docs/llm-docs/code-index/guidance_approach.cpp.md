# guidance_approach.cpp (337 lines)

Implements the unified "walk-to-act" watchdog: whoever dispatches a walk
(Shift+- autowalk, Enter loot/talk/door) arms it with target identity +
lifecycle flags, and TickApproach() drives success/blocked detection purely
off movement liveness (never action-queue depth, since UseObject's composite
queue never drains to 0). Success paths: a dialog panel opens, a foreground
UI blocks (loot), a bark bubble surfaces (off-walkmesh use-script result), or
the PC settles within kReachedMSq of the target. Blocked path: stalled
(kStallMs) while still far from target — cancels movement, restores input if
the caller disabled it, clears dialog-pending state, and speaks "way blocked"
with live distance+compass (unless the target is a scripted Endar Spire
battle soldier, which gets a dramatic in-world line instead). Talks to
engine_panels (dialog/UI-blocking state), engine_player (position, input
enable), guidance_autowalk (CancelMovement), and spectator_scene.

## Declarations (in source order)

- L19 — `namespace acc::guidance`
- L25-36 — thresholds: `kStallMs=1800`, `kCeilingMs=12000`, `kProgressEpsSq`, `kReachedMSq=36` (6m), `kCancelGraceMs=300`
- L38 — `struct ApproachState` — active/owner/name/targetObj/targetPos/inputDisabled/isDialog/speakBlocked/barkAtArm + progress tracking
- L55 — `ApproachState g_st`
- L59 — `bool ResolveTargetPos(Vector& out)`
- L74 — `bool WithinReach()`
- L88 — `void OnBlocked(DWORD stalledMs)` — cancel + restore + announce, disarms
- L130 — `void ArmApproach(const ApproachArm& arm)` — public
- L170 — `void TickApproach()` — public; per-tick success/blocked driver
- L269 — `bool IsApproachInFlight()` — public; true only for Cycle-owned arms
- L273 — `void CancelApproach()` — public
- L277 — `bool IsAnyModApproachInFlight()` — public; any owner
- L281 — `bool CancelByMovement()` — public; movement-key reclaim, respects kCancelGraceMs
- L305 — `void SpeakWayBlocked(const char* name, const Vector& targetPos)` — public
