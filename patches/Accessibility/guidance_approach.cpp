#include "guidance_approach.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetObjectPosition / GetObjectName
#include "engine_compass.h"   // EngineYawToCompass / CompassToSector / SectorString
#include "engine_panels.h"    // HasActiveDialogPanel / IsForegroundUiBlocking / SetGlobalDialogState
#include "engine_player.h"    // GetPlayerPosition / SetPlayerInputEnabled
#include "guidance_autowalk.h"// CancelMovement
#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::guidance {

namespace {

// Unified thresholds — the interact-tuned (latest, most forgiving) set. False
// "way blocked" is worse than a silent retry, so these are generous on purpose.
constexpr DWORD kStallMs        = 1800;    // sustained no-movement = settled/blocked
constexpr DWORD kCeilingMs      = 12000;   // hard backstop (unreadable state)
constexpr float kProgressEpsSq  = 0.25f;   // (0.5m)^2 — moved threshold
constexpr float kReachedMSq     = 36.0f;   // (6m)^2 — stalled within this of the
                                           // target = effectively arrived / a
                                           // difficult-terrain near-miss; disarm
                                           // quietly rather than nag. Only a stall
                                           // beyond this counts as truly blocked.

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

// Once the PC stops moving, a gap below kReachedMSq means it effectively
// arrived (or a difficult-terrain near-miss left it close) — disarm without
// nagging. Only a stall beyond that is a true "can't reach".
bool WithinReach() {
    Vector tgt;
    if (!ResolveTargetPos(tgt)) return false;
    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) return false;
    float dx = tgt.x - pos.x;
    float dy = tgt.y - pos.y;
    return (dx * dx + dy * dy) <= kReachedMSq;
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
        "speakBlocked=%d targetObj=%p name=[%s]",
        static_cast<int>(arm.owner), arm.isDialog ? 1 : 0,
        arm.inputDisabled ? 1 : 0, arm.speakBlocked ? 1 : 0,
        arm.targetObj, g_st.name);
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
        g_st.lastPos    = pos;
        g_st.progressAt = now;
        return;
    }
    if (now - g_st.progressAt < kStallMs) return;          // brief pause / still
                                                           // ramping up (the
                                                           // stall window > any
                                                           // engine start latency)

    // Stalled with no success surfaced. Either it effectively arrived (a no-panel
    // act — door/bash/mine — or a near-miss left it close: don't nag), or it
    // never got near the target (genuinely blocked). Distinguish by the live gap.
    if (WithinReach()) {
        acclog::Write("Approach", "settled within reach (stalled %lums) — disarm, "
            "no nag", static_cast<unsigned long>(now - g_st.progressAt));
        g_st.active = false;
        return;
    }
    OnBlocked(now - g_st.progressAt);
}

bool IsApproachInFlight() {
    return g_st.active && g_st.owner == ApproachOwner::Cycle;
}

void CancelApproach() {
    g_st.active = false;
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
