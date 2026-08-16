#include "guidance_approach.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

#include "engine_area.h"      // GetObjectPosition / GetObjectName
#include "engine_compass.h"   // EngineYawToCompass / CompassToSector / SectorString
#include "engine_panels.h"    // HasActiveDialogPanel / IsForegroundUiBlocking / SetGlobalDialogState
#include "engine_player.h"    // GetPlayerPosition / SetPlayerInputEnabled
#include "guidance_autowalk.h"// CancelMovement
#include "log.h"
#include "prism.h"
#include "spectator_scene.h" // Endar Spire scripted spectator-battle cue
#include "strings.h"

namespace acc::guidance {

namespace {

// Unified thresholds — the interact-tuned (latest, most forgiving) set. False
// "way blocked" is worse than a silent retry, so these are generous on purpose.
constexpr DWORD kStallMs        = 1800;    // sustained no-movement = settled/blocked
constexpr DWORD kCeilingMs      = 12000;   // hard backstop (unreadable state)
constexpr float kProgressEpsSq  = 0.25f;   // (0.5m)^2 — moved threshold
constexpr float kReachedMSq     = 36.0f;   // (6m)^2 — a walk that RAN and then
                                           // stalled within this of the target =
                                           // effectively arrived / a difficult-
                                           // terrain near-miss; disarm quietly
                                           // rather than nag. Only a stall beyond
                                           // this counts as truly blocked.
constexpr float kStoodStillMSq  = 6.25f;   // (2.5m)^2 — but when the PC never took
                                           // a step, 6m is far too generous. See
                                           // the reach-tolerance note in
                                           // TickApproach.
constexpr float kReissueUseMSq  = 16.0f;   // (4m)^2 — a retried walk that settles
                                           // this close is at the object; re-fire
                                           // the use it displaced. Generous
                                           // because the walk targets the object
                                           // centre while the walkmesh can stop
                                           // the PC a metre or two short of it.
constexpr DWORD kCancelGraceMs  = 300;     // movement-key cancel ignores the
                                           // first 300ms after a dispatch — covers
                                           // a key still held from before the walk
                                           // and the engine's enqueue ramp-up.

struct ApproachState {
    bool          active        = false;
    ApproachOwner owner         = ApproachOwner::Interact;
    char          name[128]     = "";
    void*         targetObj     = nullptr;
    Vector        targetPos     = {0.0f, 0.0f, 0.0f};
    bool          haveTargetPos = false;
    bool          inputDisabled = false;
    bool          isDialog      = false;
    bool          speakBlocked  = true;
    bool          barkAtArm     = false;

    DWORD         armedAt       = 0;
    bool          haveProgress  = false;
    Vector        lastPos       = {0.0f, 0.0f, 0.0f};
    DWORD         progressAt    = 0;

    // Did the walk ever physically start? Latched by the movement branch. False
    // at the stall verdict means the PC has not taken one step since the arm —
    // the dispatch never became motion, however healthy the queue looked.
    bool          movedSinceArm = false;
    // The coordinate-walk retry is one-shot per arm; without this a target the
    // A* also can't reach would re-dispatch every stall window forever.
    bool          coordRetried  = false;

    // Use-verb handle to re-fire once the retried walk lands (see the header
    // note on ApproachArm::useHandle), and its one-shot latch.
    uint32_t      useHandle     = 0;
    bool          useReissued   = false;
};
ApproachState g_st;

// Live target position when the object is readable, else the stamped fallback.
// Returns false when neither is available.
bool ResolveTargetPos(Vector& out) {
    if (g_st.targetObj &&
        acc::engine::GetObjectPosition(g_st.targetObj, out)) {
        return true;
    }
    if (g_st.haveTargetPos) {
        out = g_st.targetPos;
        return true;
    }
    return false;
}

// Planar player→target gap, in metres.
float GapTo(const Vector& tgt, const Vector& pos) {
    float dx = tgt.x - pos.x;
    float dy = tgt.y - pos.y;
    return std::sqrt(dx * dx + dy * dy);
}

// The walk never physically started (see the header note): re-issue it as a
// plain coordinate walk to the target position rather than hand the player a
// "way blocked" they would reasonably read as "there is no route at all".
// Returns true when a retry is in flight — caller must not announce.
//
// Gated on movement, not on the action queue. Both observed shapes of a
// never-started walk look healthy in the queue: the discarded use action leaves
// it flat (0→1→0 in ~50ms), and the unroutable one drives a re-plan loop that
// thrashes 4↔6 for the whole stall window. Neither moves the PC one step.
bool TryCoordinateWalkRetry(const Vector& tgt) {
    if (g_st.movedSinceArm || g_st.coordRetried) return false;

    // Clear the failed plan first. In the re-plan-loop case the engine is still
    // churning actions that would fight the coordinate walk; in the discarded
    // case the queue is already empty and this is a no-op.
    acc::guidance::CancelMovement();

    // WalkTo manages its own AI level and needs manual input left enabled — the
    // same reason cycle_input's map-pin branch never disables it. Hand input
    // back first and drop the flag so a later OnBlocked can't restore twice.
    if (g_st.inputDisabled) {
        acc::engine::SetPlayerInputEnabled(true);
        g_st.inputDisabled = false;
    }

    g_st.coordRetried = true;
    if (!acc::guidance::WalkTo(tgt)) {
        acclog::Write("Approach", "walk never started; coordinate-walk retry "
            "failed to queue name=[%s] — falling through to blocked", g_st.name);
        return false;
    }

    g_st.haveProgress = false;          // restart the stall window clean
    g_st.progressAt   = GetTickCount();
    acclog::Write("Approach", "walk never started (PC has not moved since arm) "
        "— retrying as coordinate walk to (%.2f,%.2f,%.2f) name=[%s]",
        tgt.x, tgt.y, tgt.z, g_st.name);
    return true;
}

// The coordinate-walk retry only walks. It has to clear the failed plan to free
// the pathfinder, so the use action it replaced is gone by the time the PC
// arrives — leaving the player standing at the object needing a second press
// (patch-20260816-105520.log: the retry lands at 1.19m, and Enter from there
// opens the console instantly). Re-fire the use once on arrival so the rescue
// completes in one press.
//
// Scoped to the retry path. A settle that never needed a retry already ran its
// action — a door bashed at arm's length — and re-firing there would act twice.
//
// Returns true when a use is in flight; the caller must not disarm. Success 1/2
// (conversation / panel) takes it from here, and if nothing surfaces the next
// stall lands back on the quiet disarm with the latch already set.
bool TryReissueUse(float gap) {
    if (!g_st.coordRetried || g_st.useReissued || g_st.useHandle == 0) return false;
    if (gap * gap > kReissueUseMSq) return false;

    g_st.useReissued = true;

    // Mirror interact_dispatch's 0x3f7 contract exactly: input off around the
    // use, tracker owns the force-restore if this ends up blocked. At this range
    // the engine resolves the use-node without a walk, so the freeze is
    // momentary and engine_player's queue-watched restore returns control when
    // the queue drains.
    bool disabled = acc::engine::SetPlayerInputEnabled(false);
    if (!acc::guidance::UseObject(g_st.useHandle)) {
        if (disabled) acc::engine::SetPlayerInputEnabled(true);
        acclog::Write("Approach", "arrived after retry but UseObject refused "
            "handle=0x%08x name=[%s] — falling through to quiet disarm",
            g_st.useHandle, g_st.name);
        return false;
    }
    g_st.inputDisabled = disabled;

    // Give the re-fired use a full stall window to open its panel before any
    // further verdict.
    g_st.haveProgress = false;
    g_st.progressAt   = GetTickCount();
    acclog::Write("Approach", "arrived after coordinate-walk retry (gap %.2fm) — "
        "re-firing use handle=0x%08x name=[%s]", gap, g_st.useHandle, g_st.name);
    return true;
}

// Break a blocked approach: cancel the bouncing walk, restore input if the
// caller disabled it (explicit, so engine_player's queue-watched restore and
// this tracker never fight), clear dialog-pending limbo (talk only), and
// announce "way blocked" when the arm asked for it. Disarms.
void OnBlocked(DWORD stalledMs) {
    acc::guidance::CancelMovement();
    if (g_st.inputDisabled) {
        acc::engine::SetPlayerInputEnabled(true);
    }
    if (g_st.isDialog) {
        acc::engine::SetGlobalDialogState(0);
    }

    if (g_st.speakBlocked) {
        // Scoped Endar Spire spectator battle: a walk toward one of the
        // doomed Republic soldiers can never land (walkmesh gap). Swap the
        // generic "way blocked" for the dramatic in-world line so the repeat
        // reinforces "you can't reach them, press on to the bridge".
        if (acc::spectator::IsScriptedBattleSoldier(g_st.targetObj)) {
            prism::Speak(acc::spectator::DramaticLine(), /*interrupt=*/true);
            acclog::Write("Approach",
                "blocked on scripted-battle soldier -> [%s]",
                acc::spectator::DramaticLine());
        } else {
            Vector tgt;
            char msg[192];
            if (ResolveTargetPos(tgt) && g_st.name[0]) {
                SpeakWayBlocked(g_st.name, tgt);
            } else {
                std::snprintf(msg, sizeof(msg), "%s",
                              acc::strings::Get(acc::strings::Id::InteractWayBlocked));
                prism::Speak(msg, /*interrupt=*/true);
            }
        }
    }

    acclog::Write("Approach", "BLOCKED — owner=%d isDialog=%d inputDisabled=%d "
        "stalled=%lums name=[%s] (cancelled approach)",
        static_cast<int>(g_st.owner), g_st.isDialog ? 1 : 0,
        g_st.inputDisabled ? 1 : 0, static_cast<unsigned long>(stalledMs),
        g_st.name);
    g_st.active = false;
}

}  // namespace

void ArmApproach(const ApproachArm& arm) {
    DWORD now = GetTickCount();
    g_st = {};
    g_st.active        = true;
    g_st.owner         = arm.owner;
    std::snprintf(g_st.name, sizeof(g_st.name), "%s",
                  (arm.name[0]) ? arm.name : "?");
    g_st.targetObj     = arm.targetObj;
    g_st.targetPos     = arm.targetPos;
    g_st.haveTargetPos = (arm.targetPos.x != 0.0f || arm.targetPos.y != 0.0f ||
                          arm.targetPos.z != 0.0f);
    g_st.inputDisabled = arm.inputDisabled;
    g_st.isDialog      = arm.isDialog;
    g_st.speakBlocked  = arm.speakBlocked;
    g_st.useHandle     = arm.useHandle;
    // Snapshot any bark already showing so a lingering ambient bubble can't
    // instantly disarm a fresh walk; only a bark that *surfaces* after arm
    // counts as this interaction's output.
    g_st.barkAtArm     = acc::engine::HasActiveBarkBubble();
    g_st.armedAt       = now;
    g_st.haveProgress  = false;
    g_st.progressAt    = now;

    // If no fallback pos was supplied but we have the object, snapshot a live
    // read now so a later announce still has a position even if the object
    // becomes unreadable mid-walk (targets are stationary).
    if (!g_st.haveTargetPos && g_st.targetObj) {
        Vector p;
        if (acc::engine::GetObjectPosition(g_st.targetObj, p)) {
            g_st.targetPos     = p;
            g_st.haveTargetPos = true;
        }
    }

    acclog::Write("Approach", "armed — owner=%d isDialog=%d inputDisabled=%d "
        "speakBlocked=%d targetObj=%p useHandle=0x%08x name=[%s]",
        static_cast<int>(arm.owner), arm.isDialog ? 1 : 0,
        arm.inputDisabled ? 1 : 0, arm.speakBlocked ? 1 : 0,
        arm.targetObj, arm.useHandle, g_st.name);
}

void TickApproach() {
    if (!g_st.active) return;
    DWORD now = GetTickCount();

    // Success 1 — a conversation opened (talk verbs). PC reached range and the
    // dialog started; nothing to cancel.
    if (acc::engine::HasActiveDialogPanel()) {
        acclog::Write("Approach", "conversation open — disarm (walk-to-talk OK)");
        g_st.active = false;
        return;
    }

    // Success 2 — an interaction panel opened (container loot puts a blocking
    // modal in the foreground). The tracker only ever arms in-world with nothing
    // blocking, so a blocker appearing now is the interaction result.
    acc::engine::UiBlockState blk;
    if (acc::engine::IsForegroundUiBlocking(&blk)) {
        acclog::Write("Approach", "interaction panel open (fgKind=%d) — disarm "
            "(walk-to-use OK)", static_cast<int>(blk.fgKind));
        g_st.active = false;
        return;
    }

    // Success 3 — the use-script delivered its result as a bark bubble rather
    // than a conversation (e.g. examining an off-walkmesh placeable: the
    // hovering swoop bikes). The bark surfacing proves the interaction fired
    // even though the body never physically arrived. Disarm quietly — never
    // CancelMovement (it would ClearAllActions and kill any queued follow-up)
    // and never announce "way blocked". engine_player's queue-watched restore
    // re-enables input on its own when the queue drains/stalls.
    if (!g_st.barkAtArm && acc::engine::HasActiveBarkBubble()) {
        acclog::Write("Approach", "bark surfaced — disarm (interaction fired)");
        g_st.active = false;
        return;
    }

    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) {
        // Blind read — don't act on it; fall back to the hard ceiling so a
        // wedged unreadable state can't keep us armed forever.
        if (now - g_st.armedAt >= kCeilingMs) {
            acclog::Write("Approach", "ceiling (position unreadable) — disarm");
            g_st.active = false;
        }
        return;
    }

    // Movement liveness. lastPos updates only when the PC has moved ≥0.5 m, so a
    // normal sub-threshold stride still registers progress every few ticks and
    // keeps resetting the stall timer — a long cross-terrain walk is never cut
    // off. Grace covers the post-dispatch ramp-up before the engine enqueues.
    if (!g_st.haveProgress) {
        g_st.lastPos      = pos;
        g_st.progressAt   = now;
        g_st.haveProgress = true;
        return;
    }
    float dx = pos.x - g_st.lastPos.x;
    float dy = pos.y - g_st.lastPos.y;
    if ((dx * dx + dy * dy) >= kProgressEpsSq) {
        g_st.lastPos       = pos;
        g_st.progressAt    = now;
        g_st.movedSinceArm = true;
        return;
    }
    if (now - g_st.progressAt < kStallMs) return;          // brief pause / still
                                                           // ramping up (the
                                                           // stall window > any
                                                           // engine start latency)

    // Stalled with no success surfaced. Either it effectively arrived (a no-panel
    // act — door/bash/mine — or a near-miss left it close: don't nag), or it
    // never got near the target (genuinely blocked). Distinguish by the live gap.
    //
    // First: if we can't resolve the target position at all, we cannot prove the
    // PC is out of range — so we must NOT cancel the (possibly still-running)
    // interaction or announce a false "way blocked". The pre-unify watchdog hit
    // exactly this: a wrong-namespace targetObj faulted GetObjectPosition and the
    // (0,0,0) fallback pos read as "miles away" → bogus "Bewegung abgebrochen"
    // mid-open (patch-20260618-223250.log, a stale build). The unify already
    // snapshots a real pos at arm so the common path resolves; this keeps the
    // blocked verdict honest for any caller whose target can't be positioned —
    // disarm quietly and let engine_player's queue-watched restore re-enable
    // input. (Mirrors the bark branch: when in doubt the interaction fired.)
    Vector tgt;
    if (!ResolveTargetPos(tgt)) {
        acclog::Write("Approach", "stalled but target position unresolvable — "
            "disarm quietly (cannot prove out of reach; not cancelling)");
        g_st.active = false;
        return;
    }
    // Reach tolerance depends on whether the walk ever physically ran — two very
    // different stalls otherwise wear the same distance.
    //
    // Walk RAN, then stalled: the engine got the PC moving and parked it wherever
    // the walkmesh allowed. Six metres of slack is right — the act very likely
    // fired (door/bash/mine open no panel) and nagging is worse than silence.
    //
    // Walk NEVER RAN (not one step since the arm): six metres is a lie. Observed
    // on the Telos comm console (patch-20260816-095834.log): nine Enter presses
    // from 4.5–5.5m out, every dispatch accepted, every one driving the engine's
    // 5↔6 re-plan churn with the PC rooted — and every one silently absorbed here
    // as "within reach" because it sat inside the 6m radius. The retry below
    // never got a turn and the player got no cue at all. At a standstill the only
    // honest reading of a small gap is "we were already at it and the act fired in
    // place", which is a use-range distance, not six metres. Keeping this branch
    // tight also protects those in-place acts: bashing a door at arm's length must
    // stay a quiet disarm and never be torn down by the retry's CancelMovement.
    const float gap     = GapTo(tgt, pos);
    const float reachSq = g_st.movedSinceArm ? kReachedMSq : kStoodStillMSq;
    if (gap * gap <= reachSq) {
        // Arrived after a coordinate-walk retry — the use it displaced still
        // owes the player its result. Fire it before calling this settled.
        if (TryReissueUse(gap)) return;
        acclog::Write("Approach", "settled within reach (stalled %lums, gap %.2fm, "
            "moved=%d) — disarm, no nag",
            static_cast<unsigned long>(now - g_st.progressAt), gap,
            g_st.movedSinceArm ? 1 : 0);
        g_st.active = false;
        return;
    }
    // Out of reach and stalled. Before calling it blocked, check whether the
    // walk ever actually started: a use dispatch the engine accepted but never
    // turned into motion leaves the PC rooted where it stood, and announcing
    // "way blocked" there tells the player no route exists when the coordinate
    // A* would have walked them straight to it.
    if (TryCoordinateWalkRetry(tgt)) return;

    OnBlocked(now - g_st.progressAt);
}

bool IsApproachInFlight() {
    return g_st.active && g_st.owner == ApproachOwner::Cycle;
}

void CancelApproach() {
    g_st.active = false;
}

void* ApproachTarget() {
    return g_st.active ? g_st.targetObj : nullptr;
}

bool IsAnyModApproachInFlight() {
    return g_st.active;
}

bool CancelByMovement() {
    if (!g_st.active) return false;
    // Arm grace: a movement key still down from before the dispatch (or the
    // engine's post-dispatch enqueue ramp-up) must not cancel the walk before
    // it has a chance to start.
    if (GetTickCount() - g_st.armedAt < kCancelGraceMs) return false;

    acc::guidance::CancelMovement();
    if (g_st.inputDisabled) {
        acc::engine::SetPlayerInputEnabled(true);
    }
    if (g_st.isDialog) {
        acc::engine::SetGlobalDialogState(0);
    }
    const char* msg = acc::strings::Get(acc::strings::Id::MovementCancelled);
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Approach", "movement-cancel — owner=%d isDialog=%d "
        "inputDisabled=%d name=[%s] (manual control reclaimed)",
        static_cast<int>(g_st.owner), g_st.isDialog ? 1 : 0,
        g_st.inputDisabled ? 1 : 0, g_st.name);
    g_st.active = false;
    return true;
}

void SpeakWayBlocked(const char* name, const Vector& targetPos) {
    char msg[192];
    bool built = false;
    if (name && name[0]) {
        Vector playerPos;
        if (acc::engine::GetPlayerPosition(playerPos)) {
            float dx = targetPos.x - playerPos.x;
            float dy = targetPos.y - playerPos.y;
            int metres = static_cast<int>(std::sqrt(dx * dx + dy * dy) + 0.5f);
            if (metres < 1) metres = 1;

            // Absolute 8-point compass of the player→target vector — same frame
            // the route readout and passive cue use (+X=East, +Y=North).
            float engineYaw = std::atan2(dy, dx) * (180.0f / 3.14159265358979f);
            int   sector    = acc::engine::CompassToSector(
                                  acc::engine::EngineYawToCompass(engineYaw));
            const char* dir = acc::strings::Get(acc::engine::SectorString(sector));

            std::snprintf(msg, sizeof(msg),
                acc::strings::Get(acc::strings::Id::FmtInteractWayBlockedTarget),
                name, metres, dir);
            built = true;
        }
    }
    if (!built) {
        std::snprintf(msg, sizeof(msg), "%s",
                      acc::strings::Get(acc::strings::Id::InteractWayBlocked));
    }
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Approach", "way blocked -> [%s]", msg);
}

}  // namespace acc::guidance
