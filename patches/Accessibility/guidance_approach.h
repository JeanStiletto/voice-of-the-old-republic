// Unified approach tracker — the single "I dispatched a walk-to-act, watch
// whether it arrives, settles, or stalls" state machine.
//
// Replaces three previously-independent observers that all keyed on the same
// signal (player movement after a dispatch):
//   - guidance_autowalk's in-flight tracker + walk-blocked one-shot (Shift+-),
//   - interact_hotkey's g_approach dialog/use watchdog (Enter),
//   - cycle_input's autowalk block-watch (Shift+- announce wrapper).
//
// One arm, one tick, one set of thresholds. Whoever dispatches a walk (Shift+-
// autowalk, Enter loot/talk/door/...) arms it with the target identity + the
// lifecycle flags; TickApproach() drives it:
//   - SUCCESS  → a conversation opens (talk), an interaction panel opens (loot),
//     or the PC settles within reach of the target. Disarm quietly.
//   - BLOCKED  → the PC stalls (no movement for the stall window) while still
//     far from the target (wedged against geometry / no walkable point in
//     range). Cancel the bouncing walk, restore input if the caller disabled
//     it, clear dialog-pending state (talk only), and — if the arm asked for
//     it — announce "way blocked" with the target's live distance + direction.
//
// Liveness is MOVEMENT, never the action-queue depth: the engine drains its
// queue to 0 transiently between walkmesh waypoints and UseObject's composite
// queue never drains, so a depth read mistakes live walks for finished ones.
//
// That same movement signal also sets how much distance "settled within reach"
// forgives. A walk that ran and wedged gets six metres of slack; a walk that
// never took a step gets only use range, because at a standstill a small gap can
// only mean "we were already at it". Six metres there swallowed nine consecutive
// failed console interactions whole (patch-20260816-095834.log) — each one was
// inside the radius, so it disarmed silently and the retry below never ran.
//
// That same movement signal separates the two verdicts a stall can carry. A
// walk that started and then wedged has moved at least once; a walk that never
// started has not moved at all, and for that case "way blocked" is a lie — the
// player hears "there is no route" when the route is fine and only the dispatch
// failed. AddUseObjectAction produces both shapes and reports success for each:
// it discards the action a frame later when it cannot resolve a use-node (queue
// 0→1→0 in ~50ms), or it drives a re-plan loop that churns the queue for the
// whole stall window (4↔6, PC rooted). So on a stall with no movement since the
// arm, the tracker clears the failed plan and retries once as a plain
// coordinate walk — the engine's coordinate A* routes over distances the use
// action refuses — and only announces "way blocked" if that stalls too.
//
// Input restore on SUCCESS stays with engine_player's queue-watched session
// (TickPlayerInputRestore) — it knows the queue state. This tracker only
// force-restores on the BLOCKED path, where it is actively intervening, so the
// two never fight.

#pragma once

#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::guidance {

// Who armed the approach. Controls toggle-cancel eligibility: only a
// Cycle-owned (Shift+-) walk is cancellable by a second Shift+- press or a
// W/S/A/D panic key. Enter dispatches are not (matching prior behaviour, where
// the Enter and Shift+- trackers were deliberately separate).
enum class ApproachOwner { Cycle, Interact };

struct ApproachArm {
    ApproachOwner owner        = ApproachOwner::Interact;
    char          name[128]    = "";        // pre-resolved target name (way-blocked)
    void*         targetObj    = nullptr;   // live-pos source; nullptr → targetPos only
    Vector        targetPos    = {0.0f, 0.0f, 0.0f};  // arrival ref / fallback pos
    bool          inputDisabled = false;    // caller disabled input → force-restore on block
    bool          isDialog      = false;    // clear global dialog state on block
    bool          speakBlocked  = true;     // announce "way blocked" on stall-out-of-range
};

// Arm / replace the single in-flight approach observation. A fresh arm
// supersedes any prior one (only one walk in flight at a time).
void ArmApproach(const ApproachArm& arm);

// Per-tick driver. Cheap when idle (one flag check). Call once per tick.
void TickApproach();

// True while a Cycle-owned approach is in flight — the Shift+- second-press
// toggle-cancel queries this. Interact-owned arms read as not-in-flight here so
// the Shift+- toggle can't cancel an Enter walk.
bool IsApproachInFlight();

// True while ANY mod-armed approach is in flight, regardless of owner. The
// movement-key cancel queries this: both Shift+- (Cycle) and Enter (Interact)
// walks should yield to manual movement. Game/cutscene-driven movement never
// arms the tracker, so it is structurally out of this function's reach.
bool IsAnyModApproachInFlight();

// Movement-key cancel: reclaim manual control from an in-flight mod-armed walk.
// Owner-agnostic teardown — cancels the bouncing walk, restores input if the
// arming caller disabled it, clears dialog-pending state (Enter walk-to-talk),
// speaks "movement cancelled", and disarms. No-op (returns false) when nothing
// is armed or the arm grace (kCancelGraceMs) hasn't elapsed, so a movement key
// still held from before the dispatch can't kill the walk before it starts.
bool CancelByMovement();

// Clear tracker state without announcing — for the explicit Shift+- / panic
// cancel paths, where the caller already runs CancelMovement + input restore.
void CancelApproach();

// The object an in-flight approach is walking to, or nullptr when nothing is
// armed. For identity comparison only — never dereference it; the tracker holds
// the pointer across the whole walk and does not revalidate it. Lets a dispatch
// site recognise "this press is a re-press against the target we are already
// trying to reach" and tear the stale attempt down instead of stacking onto it.
void* ApproachTarget();

// Announce "way blocked" for a target with the given name + LIVE distance and
// compass direction (player→target), falling back to the plain phrase when the
// player position is unreadable. Standalone so callers that want a one-shot
// announce without arming the tracker can reuse the exact phrasing.
void SpeakWayBlocked(const char* name, const Vector& targetPos);

}  // namespace acc::guidance
