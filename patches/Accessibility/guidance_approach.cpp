#include "guidance_approach.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include "bringup_announce.h" // IsMoviePlaying — a movie ends the walk silently
#include "combat.h"           // IsPartyInCombat — combat-interrupted verdict
#include "engine_area.h"      // GetObjectPosition / GetObjectName / GetCurrentArea
#include "engine_compass.h"   // EngineYawToCompass / CompassToSector / SectorString
#include "engine_panels.h"    // HasActiveDialogPanel / IsForegroundUiBlocking / SetGlobalDialogState
#include "engine_picker.h"    // InitiateDialog / Drive — legged walk's arrival acts
#include "engine_player.h"    // GetPlayerPosition / SetPlayerInputEnabled
#include "engine_subscreen.h" // WorldIsPaused — the stall clock stops while paused
#include "guidance_autowalk.h"// CancelMovement
#include "guidance_pathfind.h"// ComputePath — long-route start allowance
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
// Long-route start allowance. The engine plans a long walk over the area's
// path-point graph, which (now that split K2 graphs are linked) can route
// 200 m+ across a map — planning latency for that was never measured, and
// the 1.8 s "never started" verdict is tuned on short walks. When OUR route
// finder proves the target reachable and the route is long, give the first
// step up to this long before the never-started retry. Short routes keep the
// 1.8 s verdict (the Telos console rescue stays fast), unreachable ones too.
// "Approach: first step after" lines record the real latency for tuning.
constexpr DWORD kLongRouteStartMs = 6000;
constexpr float kLongRouteM       = 40.0f;
constexpr DWORD kCancelGraceMs  = 300;     // movement-key cancel ignores the
                                           // first 300ms after a dispatch — covers
                                           // a key still held from before the walk
                                           // and the engine's enqueue ramp-up.
// Legged walk (BeginLeggedApproach). The engine's planner fails at 51 path
// points and at 4096 expansions per search pass; the second cap bites well
// before the first in a dense network, and no live witness yet says where.
// Routes up to kEngineRouteMaxPoints keep the engine's own single walk (the
// 58 m and 149 m Malachor walks started in 0.4 s); longer ones walk legs of
// kLegPoints. "Approach: leg" lines record each leg's start for tuning.
constexpr int   kEngineRouteMaxPoints = 40;
constexpr int   kLegPoints            = 30;
constexpr int   kMaxLegs              = 16;     // 480 points — past kMaxNodes'
                                                // reach in practice
constexpr float kLegHandoffMSq        = 9.0f;   // (3m)^2 — next leg queued this
                                                // close to the current leg's end,
                                                // before the walk slows to a stop

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
    // Our own route to the target, checked once at the first never-started
    // verdict: 0 = not checked, 1 = long + reachable, 2 = otherwise.
    int           longRoute     = 0;

    // Use-verb handle to re-fire once the retried walk lands (see the header
    // note on ApproachArm::useHandle), and its one-shot latch.
    uint32_t      useHandle     = 0;
    bool          useReissued   = false;

    // Interruption context, snapshotted at arm. A walk dispatched in combat
    // keeps the ordinary verdicts; only combat that BEGAN after the dispatch
    // earns the combat message. The area pointer changes on every area load.
    bool          combatAtArm   = false;
    void*         areaAtArm     = nullptr;
    // Last tick time — a paused world shifts armedAt/progressAt by the gap so
    // the pause never counts as stall or start latency.
    DWORD         lastTickAt    = 0;

    // Legged walk. legEnd[0..legCount) are path-point positions on our route;
    // legIdx is the leg being walked. Once the last leg end is reached the
    // arrival act fires and the tracker continues as an ordinary approach.
    bool          legged        = false;
    int           legCount      = 0;
    int           legIdx        = 0;
    Vector        legEnd[kMaxLegs] = {};
    ArrivalAct    arrivalAct    = ArrivalAct::None;
    uint32_t      arrivalHandle = 0;
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

// Never-started on a LONG reachable route: the engine may still be planning.
// Returns true while we should keep waiting (caller must not retry/announce).
// Only the first-step window is extended — once the PC has moved, the normal
// 1.8 s stall rule applies unchanged.
bool WaitForLongRouteStart(const Vector& tgt, const Vector& pos, DWORD now) {
    if (g_st.movedSinceArm) return false;
    if (g_st.longRoute == 0) {
        g_st.longRoute = 2;
        std::vector<Vector> wps;
        if (acc::guidance::ComputePath(acc::engine::GetCurrentArea(), pos, tgt, wps) &&
            !wps.empty()) {
            float len = 0.0f;
            Vector prev = pos;
            for (const Vector& w : wps) { len += GapTo(w, prev); prev = w; }
            if (len >= kLongRouteM) g_st.longRoute = 1;
            acclog::Write("Approach", "never started yet; own route %.0fm (%s) name=[%s]",
                          len, g_st.longRoute == 1 ? "long — allowing planning time"
                                                   : "short — normal verdict",
                          g_st.name);
        }
    }
    if (g_st.longRoute != 1) return false;
    return now - g_st.armedAt < kLongRouteStartMs;
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
//
// byCombat: combat began after the dispatch and the character stood still
// since — the fight stopped the walk, not the walkmesh. Same teardown minus
// the ClearAllActions, which would also wipe the attacks the engine queues
// on the leader each combat round, and a message that names the real cause.
void OnBlocked(DWORD stalledMs, bool byCombat) {
    if (!byCombat) acc::guidance::CancelMovement();
    if (g_st.inputDisabled) {
        acc::engine::SetPlayerInputEnabled(true);
    }
    if (g_st.isDialog) {
        acc::engine::SetGlobalDialogState(0);
    }

    if (byCombat) {
        if (g_st.speakBlocked) {
            prism::Speak(acc::strings::Get(acc::strings::Id::MovementInterruptedCombat),
                         /*interrupt=*/true);
        }
        acclog::Write("Approach", "INTERRUPTED by combat — owner=%d isDialog=%d "
            "inputDisabled=%d stalled=%lums legged=%d leg=%d/%d name=[%s] "
            "(walk kept, input restored)",
            static_cast<int>(g_st.owner), g_st.isDialog ? 1 : 0,
            g_st.inputDisabled ? 1 : 0, static_cast<unsigned long>(stalledMs),
            g_st.legged ? 1 : 0, g_st.legIdx + 1, g_st.legCount, g_st.name);
        g_st.active = false;
        return;
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
        "stalled=%lums legged=%d leg=%d/%d name=[%s] (cancelled approach)",
        static_cast<int>(g_st.owner), g_st.isDialog ? 1 : 0,
        g_st.inputDisabled ? 1 : 0, static_cast<unsigned long>(stalledMs),
        g_st.legged ? 1 : 0, g_st.legIdx + 1, g_st.legCount, g_st.name);
    g_st.active = false;
}

// Silent disarm for story taking over (conversation, movie, area change) or
// the player's own next action. Touches neither the engine queue nor input:
// scripts own the character now, and engine_player's queue-watched restore
// hands input back when its session drains or stalls.
void DisarmQuietly(const char* why) {
    acclog::Write("Approach", "%s — disarm quietly (legged=%d leg=%d/%d name=[%s])",
                  why, g_st.legged ? 1 : 0, g_st.legIdx + 1, g_st.legCount,
                  g_st.name);
    g_st.active = false;
}

// Last leg reached: fire the act the caller would have dispatched, from a
// distance the engine plans in one go, and continue as an ordinary approach
// (stall / settle / success verdicts unchanged). The act lands on top of the
// leg walk still finishing its last three metres; queued behind it or
// replacing it, the engine plans from here either way.
void FireArrivalAct() {
    g_st.legged = false;
    g_st.useReissued = true;   // the act fires here; never a second time
    bool ok = false;
    const char* what = "walk";
    const uint32_t h = g_st.arrivalHandle;
    switch (g_st.arrivalAct) {
        case ArrivalAct::Use: {
            what = "use";
            bool disabled = acc::engine::SetPlayerInputEnabled(false);
            ok = acc::guidance::UseObject(h);
            if (ok) g_st.inputDisabled = disabled;
            else if (disabled) acc::engine::SetPlayerInputEnabled(true);
            break;
        }
        case ArrivalAct::Talk:
            what = "talk";
            ok = acc::picker::InitiateDialog(h);
            if (ok) g_st.isDialog = true;
            break;
        case ArrivalAct::EngineVerb: {
            what = "engine verb";
            acc::picker::ActionSnapshot snap = {};
            ok = acc::picker::Drive(h, &snap, /*forceRadial=*/false,
                                    /*populateOnly=*/false);
            break;
        }
        case ArrivalAct::None:
            break;
    }
    // No act, or the act refused: finish as a coordinate walk to the target.
    Vector tgt;
    if (!ok && ResolveTargetPos(tgt)) {
        ok = acc::guidance::WalkTo(tgt);
        if (g_st.arrivalAct != ArrivalAct::None) what = "act refused -> walk";
    }
    g_st.haveProgress = false;          // fresh stall window for the last stretch
    g_st.progressAt   = GetTickCount();
    acclog::Write("Approach", "leg %d/%d reached — arrival %s handle=0x%08x ok=%d "
        "name=[%s]", g_st.legCount, g_st.legCount, what, h, ok ? 1 : 0, g_st.name);
}

// Queue the next leg as the current one nears its end (or fire the arrival
// act after the last).
void AdvanceLeg() {
    ++g_st.legIdx;
    if (g_st.legIdx >= g_st.legCount) {
        FireArrivalAct();
        return;
    }
    const Vector& end = g_st.legEnd[g_st.legIdx];
    bool ok = acc::guidance::WalkTo(end);
    acclog::Write("Approach", "leg %d/%d reached — next leg to (%.2f,%.2f) ok=%d "
        "name=[%s]", g_st.legIdx, g_st.legCount, end.x, end.y, ok ? 1 : 0,
        g_st.name);
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
    g_st.lastTickAt    = now;
    g_st.combatAtArm   = acc::combat::IsPartyInCombat();
    g_st.areaAtArm     = acc::engine::GetCurrentArea();

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
    const DWORD sinceLast = now - g_st.lastTickAt;
    g_st.lastTickAt = now;

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

    // Story took over: a movie, or a load into another area (scripted
    // transition, cutscene module). Nothing to judge and nothing to say —
    // a system message here would talk over the scene. A null area pointer
    // mid-load counts as changed; an arm that never read one skips the test.
    if (acc::bringup_announce::IsMoviePlaying()) {
        DisarmQuietly("movie playing");
        return;
    }
    if (g_st.areaAtArm && acc::engine::GetCurrentArea() != g_st.areaAtArm) {
        DisarmQuietly("area changed");
        return;
    }

    // World paused (pause key, combat auto-pause, a menu holding the world):
    // the walk waits with it, so the clock stops — shift the arm and progress
    // stamps by the paused tick instead of judging a frozen character.
    if (acc::engine::WorldIsPaused()) {
        g_st.armedAt    += sinceLast;
        g_st.progressAt += sinceLast;
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

    // Legged walk: queue the next leg (or fire the arrival act) as the
    // character nears the current leg's end. Distance-driven, so it works
    // whether the engine appends the new walk or replaces the running one.
    if (g_st.legged && g_st.legIdx < g_st.legCount) {
        const Vector& end = g_st.legEnd[g_st.legIdx];
        float ex = pos.x - end.x;
        float ey = pos.y - end.y;
        if (ex * ex + ey * ey <= kLegHandoffMSq) AdvanceLeg();
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
        if (!g_st.movedSinceArm) {
            Vector t;
            acclog::Write("Approach", "first step after %lums (gap %.1fm) name=[%s]",
                          static_cast<unsigned long>(now - g_st.armedAt),
                          ResolveTargetPos(t) ? GapTo(t, pos) : -1.0f, g_st.name);
        }
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
    // A leg walk that settled short of its leg end — the engine parks the
    // character where the walkmesh allows, a few metres off a path point in a
    // corner — has done its leg; walk on rather than call the route blocked.
    if (g_st.legged && g_st.legIdx < g_st.legCount) {
        const Vector& end = g_st.legEnd[g_st.legIdx];
        float ex = pos.x - end.x;
        float ey = pos.y - end.y;
        if (ex * ex + ey * ey <= kReachedMSq) {
            acclog::Write("Approach", "leg %d/%d settled %.1fm short of its end — "
                "advancing name=[%s]", g_st.legIdx + 1, g_st.legCount,
                std::sqrt(ex * ex + ey * ey), g_st.name);
            g_st.progressAt = now;
            AdvanceLeg();
            return;
        }
    }

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
    //
    // Combat that began after the dispatch is the clear cause when the
    // character stands still in it — checked first, so neither the start
    // allowance nor the retry spends a walk the fight already stopped.
    if (!g_st.combatAtArm && acc::combat::IsPartyInCombat()) {
        OnBlocked(now - g_st.progressAt, /*byCombat=*/true);
        return;
    }
    // A legged walk already walks routes the engine cannot plan; the start
    // allowance and the coordinate retry would only re-ask the engine for
    // the route it refused.
    if (!g_st.legged) {
        if (WaitForLongRouteStart(tgt, pos, now)) return;
        if (TryCoordinateWalkRetry(tgt)) return;
    }

    OnBlocked(now - g_st.progressAt, /*byCombat=*/false);
}

bool BeginLeggedApproach(const ApproachArm& arm) {
    Vector goal = arm.targetPos;
    if (arm.targetObj) {
        Vector live;
        if (acc::engine::GetObjectPosition(arm.targetObj, live)) goal = live;
    }
    if (goal.x == 0.0f && goal.y == 0.0f && goal.z == 0.0f) return false;
    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) return false;

    std::vector<Vector> wps;
    std::vector<Vector> nodes;
    if (!acc::guidance::ComputePath(acc::engine::GetCurrentArea(), pos, goal, wps,
                                    nullptr, &nodes)) {
        return false;   // unreachable / unreadable: the caller's dispatch and
                        // the ordinary verdicts handle it as before
    }
    const int n = static_cast<int>(nodes.size());
    if (n <= kEngineRouteMaxPoints) return false;

    // Leg ends every kLegPoints points along our route, until the rest — last
    // leg end to the goal node — fits one engine plan.
    Vector ends[kMaxLegs];
    int count = 0;
    int last  = 0;
    while ((n - 1) - last > kLegPoints) {
        if (count == kMaxLegs) {
            acclog::Write("Approach", "legged walk: route of %d points needs more "
                "than %d legs — engine dispatch as usual name=[%s]",
                n, kMaxLegs, arm.name);
            return false;
        }
        last += kLegPoints;
        ends[count++] = nodes[last];
    }

    ArmApproach(arm);
    g_st.inputDisabled = false;     // legs are input-enabled coordinate walks
    g_st.isDialog      = false;     // set by the Talk act, not before
    g_st.legged        = true;
    g_st.legCount      = count;
    g_st.legIdx        = 0;
    for (int i = 0; i < count; ++i) g_st.legEnd[i] = ends[i];
    g_st.arrivalAct    = arm.arrivalAct;
    g_st.arrivalHandle = arm.arrivalHandle;

    if (!acc::guidance::WalkTo(ends[0])) {
        acclog::Write("Approach", "legged walk: first leg failed to queue — "
            "engine dispatch as usual name=[%s]", arm.name);
        g_st.active = false;
        return false;
    }
    acclog::Write("Approach", "legged walk: route %d points > %d — %d leg(s) of "
        "%d points, then arrival act %d handle=0x%08x; leg 1 to (%.2f,%.2f) "
        "name=[%s]", n, kEngineRouteMaxPoints, count, kLegPoints,
        static_cast<int>(arm.arrivalAct), arm.arrivalHandle,
        ends[0].x, ends[0].y, arm.name);
    return true;
}

void YieldToPlayerAction(const char* reason) {
    if (!g_st.active) return;
    char why[96];
    std::snprintf(why, sizeof(why), "player action (%s)", reason ? reason : "?");
    DisarmQuietly(why);
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
