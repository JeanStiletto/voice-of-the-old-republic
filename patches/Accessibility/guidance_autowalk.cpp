#include "guidance_autowalk.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_player.h"
#include "log.h"

namespace acc::guidance {

namespace {

// Full 17-arg signature decoded in investigation §Q3. The function packs
// runFlag / forceFlag / pathMode / avoidFlags / flagBit3 / flagBit9 /
// flagBit10 into a single bitfield, then forwards a typed-arg list to
// CSWSObject::AddAction with ACTION_MOVE_TO_POINT=1. It also calls
// SetLockOrientationToObject(this, 0x7f000000, 0) internally to release
// any prior facing lock; callers don't need to do that themselves.
typedef unsigned int (__thiscall* PFN_AddMoveToPointAction)(
    void*           this_,
    unsigned short  actionId,
    Vector*         destination,
    unsigned long   objectId1,
    unsigned long   objectId2,
    int             runFlag,
    float           radius,
    float           followDistance,
    int             forceFlag,
    int             timeoutMs,
    int             pathMode,
    int             avoidFlags,
    int             flagBit3,
    int             flagBit9,
    Vector*         secondaryPoint,
    unsigned long   pathContext1,
    unsigned long   pathContext2,
    int             flagBit10);

// CSWSForcedAction layout — per swkotor.exe.h Ghidra DATATYPEs dump.
// 28 bytes total. Used as the single argument to ForceMoveToPoint.
//
//   +0x00 ulong action_id
//   +0x04 ushort group_id
//   +0x06 (2-byte padding / undefined)
//   +0x08 ulong target_area     // area handle, INVALID = 0x7f000000
//   +0x0c Vector target_loc     // world position
//   +0x18 ulong target_object   // object handle, INVALID = 0x7f000000
struct CSWSForcedAction {
    unsigned int    action_id;
    unsigned short  group_id;
    unsigned short  pad;
    unsigned int    target_area;
    Vector          target_loc;
    unsigned int    target_object;
};
static_assert(sizeof(CSWSForcedAction) == 0x1c,
              "CSWSForcedAction must be 28 bytes per swkotor.exe.h");

typedef void (__thiscall* PFN_ForceMoveToPoint)(
    void*              this_,
    CSWSForcedAction*  action);

// Progress-watchdog state. Captured on every WalkTo / ForceWalkTo so
// subsequent OnUpdate ticks can correlate position deltas against the
// dispatch. Module-scope statics (single in-flight autowalk; new
// dispatch resets the watchdog and supersedes any in-flight observation).
struct WatchdogState {
    bool          active        = false;
    Vector        startPos      = {0.0f, 0.0f, 0.0f};
    Vector        dest          = {0.0f, 0.0f, 0.0f};
    DWORD         dispatchTick  = 0;     // GetTickCount() at dispatch
    bool          firedAt1s     = false;
    bool          firedAt3s     = false;
    bool          haveStartPos  = false; // false if pre-dispatch read failed
    const char*   tag           = "?";   // "WalkTo" / "Force" — log prefix
};
WatchdogState g_watchdog;

// In-flight tracker — distinct from the diagnostic watchdog. The
// watchdog only fires twice (t+1s, t+3s) and self-disengages, but
// "is the player still autowalking?" can persist far longer (long
// cross-area moves). We track it independently so cycle_input's
// toggle-cancel semantics work for the full duration of a walk.
//
// Set on successful dispatch (WalkTo / ForceWalkTo). Cleared on:
//   - explicit CancelMovement,
//   - per-tick distance check observing arrival (dist < 1.0m),
//   - player creature unresolvable (un-loaded mid-flight).
//
// Single-instance (only one autowalk in flight at a time; new dispatch
// supersedes prior). No thread safety — patch is single-threaded.
struct InFlightState {
    bool   active = false;
    Vector dest   = {0.0f, 0.0f, 0.0f};
};
InFlightState g_inFlight;

// Helper to arm the watchdog after a successful dispatch. Same shape
// regardless of which engine entry point did the dispatch — only the
// log prefix differs.
void ArmWatchdog(const Vector& startPos, bool haveStart,
                 const Vector& dest, const char* tag) {
    g_watchdog.active        = true;
    g_watchdog.startPos      = startPos;
    g_watchdog.dest          = dest;
    g_watchdog.dispatchTick  = GetTickCount();
    g_watchdog.firedAt1s     = false;
    g_watchdog.firedAt3s     = false;
    g_watchdog.haveStartPos  = haveStart;
    g_watchdog.tag           = tag;
}

float HorizontalDistance(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

bool WalkTo(const Vector& destination) {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) return false;

    // Caller-assigned action queue id. Engine maintains its own internal
    // queue index; this id is the caller's tag for matching responses if
    // we ever query the queue's state. ushort wraparound is harmless —
    // engine doesn't enforce uniqueness across a wrap and we don't read
    // results back.
    static unsigned short s_actionId = 0;

    // Defensive copy out of the SEH frame's reach (matches audio_bus
    // PlayCue3D's pattern). Used as both primary and secondary point —
    // primary = move destination, secondary = "look-at on arrival" per
    // §Q3 (only X/Y are read by the engine for the secondary).
    Vector dest = destination;

    // Snapshot the player's pre-dispatch position so the watchdog can
    // measure displacement-since-press. If this read fails we still
    // dispatch — the watchdog just logs without a baseline.
    Vector startPos = {0.0f, 0.0f, 0.0f};
    bool haveStart = acc::engine::GetPlayerPosition(startPos);

    unsigned int ret = 0;
    unsigned short thisActionId = s_actionId++;

    // Disable the per-tick player-input movement clobber for the duration
    // of the AI action — see project_player_control_toggle.md. Engine's
    // TickPlayerInputRestore auto-restores after ~3s; SEH-fault path
    // restores immediately.
    bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);

    __try {
        auto fn = reinterpret_cast<PFN_AddMoveToPointAction>(
            kAddrCSWSCreatureAddMoveToPointAction);
        ret = fn(creature,
           thisActionId,
           &dest,
           kInvalidObjectId, kInvalidObjectId,
           /*runFlag=*/0,
           /*radius=*/0.0f,
           /*followDistance=*/0.0f,
           /*forceFlag=*/0,
           /*timeoutMs=*/0,
           /*pathMode=*/0,
           /*avoidFlags=*/0,
           /*flagBit3=*/0,
           /*flagBit9=*/0,
           &dest,
           /*pathContext1=*/0,
           /*pathContext2=*/0,
           /*flagBit10=*/0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Disarm watchdog — no point measuring progress when the call
        // itself faulted.
        g_watchdog.active = false;
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        acclog::Write("Autowalk: WalkTo SEH-FAULT action_id=%u "
                      "dest=(%.2f,%.2f,%.2f)",
                      static_cast<unsigned>(thisActionId),
                      dest.x, dest.y, dest.z);
        return false;
    }

    // Arm the watchdog. New dispatch supersedes any prior in-flight
    // observation — the user pressed Shift+- again or a new caller fired,
    // so the prior baseline is no longer the relevant reference point.
    ArmWatchdog(startPos, haveStart, dest, "WalkTo");

    g_inFlight.active = true;
    g_inFlight.dest   = dest;

    float distToDest = haveStart ? HorizontalDistance(startPos, dest) : -1.0f;
    acclog::Write("Autowalk: WalkTo dispatch dest=(%.2f,%.2f,%.2f) "
                  "from=(%.2f,%.2f,%.2f) dist=%.2fm action_id=%u "
                  "ret=0x%08x",
                  dest.x, dest.y, dest.z,
                  startPos.x, startPos.y, startPos.z,
                  distToDest,
                  static_cast<unsigned>(thisActionId),
                  ret);

    return true;
}

bool ForceWalkTo(const Vector& destination) {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) return false;

    static unsigned int s_actionId = 0;
    Vector dest = destination;

    // Construct the forced-action struct on the stack. INVALID handles
    // for area + object — we have a world position but no specific
    // area/object reference. If the engine requires real handles for
    // these slots, the call will fault and SEH catches it.
    CSWSForcedAction action;
    action.action_id     = s_actionId++;
    action.group_id      = 0;
    action.pad           = 0;
    action.target_area   = kInvalidObjectId;
    action.target_loc    = dest;
    action.target_object = kInvalidObjectId;

    Vector startPos = {0.0f, 0.0f, 0.0f};
    bool haveStart = acc::engine::GetPlayerPosition(startPos);

    bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);

    __try {
        auto fn = reinterpret_cast<PFN_ForceMoveToPoint>(
            kAddrCSWSCreatureForceMoveToPoint);
        fn(creature, &action);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_watchdog.active = false;
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        acclog::Write("Autowalk: Force-dispatch SEH-FAULT action_id=%u "
                      "dest=(%.2f,%.2f,%.2f)",
                      action.action_id, dest.x, dest.y, dest.z);
        return false;
    }

    ArmWatchdog(startPos, haveStart, dest, "Force");

    g_inFlight.active = true;
    g_inFlight.dest   = dest;

    float distToDest = haveStart ? HorizontalDistance(startPos, dest) : -1.0f;
    acclog::Write("Autowalk: Force-dispatch dest=(%.2f,%.2f,%.2f) "
                  "from=(%.2f,%.2f,%.2f) dist=%.2fm action_id=%u "
                  "(no ret — void)",
                  dest.x, dest.y, dest.z,
                  startPos.x, startPos.y, startPos.z,
                  distToDest, action.action_id);
    return true;
}

bool UseObject(unsigned long targetHandle) {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) return false;

    if (targetHandle == 0u || targetHandle == 0xFFFFFFFFu ||
        targetHandle == kInvalidObjectId) {
        return false;
    }

    typedef int (__thiscall* PFN_AddUseObjectAction)(
        void* this_, unsigned long target, unsigned long param2);

    int ret = 0;
    __try {
        auto fn = reinterpret_cast<PFN_AddUseObjectAction>(
            kAddrCSWSObjectAddUseObjectAction);
        ret = fn(creature, targetHandle, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Autowalk: UseObject SEH-FAULT target=0x%08lx",
                      targetHandle);
        return false;
    }

    acclog::Write("Autowalk: UseObject dispatch target=0x%08lx ret=%d",
                  targetHandle, ret);
    return ret != 0;
}

bool CancelMovement() {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        // Even with no creature, clear our local state — it's
        // definitively stale.
        g_inFlight.active = false;
        g_watchdog.active = false;
        return false;
    }

    typedef void (__thiscall* PFN_ClearAllActions)(void* this_, int param_1);

    bool ok = true;
    __try {
        auto fn = reinterpret_cast<PFN_ClearAllActions>(
            kAddrCSWSObjectClearAllActions);
        // param_1 = 0 — semantics not fully decoded; first attempt with 0
        // (the safe default for "give me the standard clear behaviour").
        // If in-game testing shows queued actions persist, escalate to 1.
        fn(creature, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
        acclog::Write("Autowalk: CancelMovement SEH-FAULT");
    }

    // Clear local state regardless of engine call success — at minimum,
    // the user said "stop", so don't pretend we're still in flight.
    g_inFlight.active = false;
    g_watchdog.active = false;

    if (ok) {
        acclog::Write(
            "Autowalk: CancelMovement dispatched (ClearAllActions(0))");
    }
    return ok;
}

bool IsAutowalkInFlight() {
    return g_inFlight.active;
}

void TickProgressWatchdog() {
    // In-flight arrival check — runs even when the diagnostic watchdog has
    // self-disengaged. Cheap (one position read + horizontal-distance
    // compare) and only when actually in flight.
    if (g_inFlight.active) {
        Vector pos;
        if (!acc::engine::GetPlayerPosition(pos)) {
            g_inFlight.active = false;  // player gone, definitively done
        } else if (HorizontalDistance(pos, g_inFlight.dest) < 1.0f) {
            g_inFlight.active = false;  // arrived
        }
    }

    if (!g_watchdog.active) return;

    DWORD now = GetTickCount();
    DWORD elapsedMs = now - g_watchdog.dispatchTick;

    // We need a current position to compare against the dispatch baseline.
    // If reading fails (player un-loaded mid-flight, area teardown), shut
    // down the watchdog cleanly — there's nothing useful to log.
    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) {
        g_watchdog.active = false;
        return;
    }

    // Two checkpoints. After the second, disengage — beyond ~3 seconds
    // we can't distinguish "engine still pathing" from "user took manual
    // control" without extra state, and the diagnostic question
    // ("did the engine actually move us?") is already answered by t+1s.
    if (!g_watchdog.firedAt1s && elapsedMs >= 1000) {
        g_watchdog.firedAt1s = true;
        if (g_watchdog.haveStartPos) {
            float moved = HorizontalDistance(g_watchdog.startPos, pos);
            float distToDest = HorizontalDistance(pos, g_watchdog.dest);
            const char* state = (moved < 0.1f) ? "stuck" : "moving";
            acclog::Write("Autowalk: %s t+1s moved=%.2fm dist=%.2fm (%s)",
                          g_watchdog.tag, moved, distToDest, state);
        } else {
            // No baseline: still useful — we know whether we're near the
            // destination at the 1s mark.
            float distToDest = HorizontalDistance(pos, g_watchdog.dest);
            acclog::Write("Autowalk: %s t+1s dist=%.2fm (no baseline)",
                          g_watchdog.tag, distToDest);
        }
    }

    if (!g_watchdog.firedAt3s && elapsedMs >= 3000) {
        g_watchdog.firedAt3s = true;
        float distToDest = HorizontalDistance(pos, g_watchdog.dest);
        if (g_watchdog.haveStartPos) {
            float moved = HorizontalDistance(g_watchdog.startPos, pos);
            const char* state =
                (distToDest < 1.0f) ? "reached"      :
                (moved      < 0.1f) ? "still stuck"  :
                                       "moving";
            acclog::Write("Autowalk: %s t+3s moved=%.2fm dist=%.2fm (%s)",
                          g_watchdog.tag, moved, distToDest, state);
        } else {
            const char* state = (distToDest < 1.0f) ? "reached" : "unknown";
            acclog::Write("Autowalk: %s t+3s dist=%.2fm (%s, no baseline)",
                          g_watchdog.tag, distToDest, state);
        }
        // Disengage. Future user actions (next Shift+-, etc.) will re-arm.
        g_watchdog.active = false;
    }
}

}  // namespace acc::guidance
