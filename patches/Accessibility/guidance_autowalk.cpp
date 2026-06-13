#include "guidance_autowalk.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "engine_player.h"
#include "hotkeys.h"  // IsForegroundGame — gate movement-key cancel polling
                      // so keys pressed in another app while Alt+Tabbed out
                      // don't kill an in-flight autowalk silently.
#include "log.h"
#include "prism.h"
#include "strings.h"

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
    bool   active        = false;
    Vector dest          = {0.0f, 0.0f, 0.0f};
    DWORD  dispatchTick  = 0;       // GetTickCount() at dispatch
    bool   sawPending    = false;   // latched once the move is seen queued
    Vector lastPos       = {0.0f, 0.0f, 0.0f};
    DWORD  lastMoveTick  = 0;       // last time the PC actually moved
    bool   sawMoving     = false;   // latched once the PC has moved at all
    bool   haveLastPos   = false;   // lastPos seeded yet?
};
InFlightState g_inFlight;

// Post-dispatch grace: the engine may not have enqueued / started the move on
// the first tick(s) after dispatch, so "drained" / "still" reads in that
// window must NOT be treated as "done".
constexpr DWORD kInFlightGraceMs = 500;
// The walk is over once the PC has been motionless this long. This is the
// signal that works for BOTH dispatch kinds: WalkTo's move drains to 0 (PC
// stops), and UseObject's composite queue never drains (it oscillates 4↔6
// through the walk and the conversation it opens) but the PC still stops once
// it reaches use-range. Queue-depth==0 alone misses the UseObject case.
constexpr DWORD kInFlightStillMs = 1000;
// Per-tick movement threshold (squared metres). Below this the PC is "still".
constexpr float kInFlightMoveEpsSq = 0.04f;  // (0.2m)^2
// A walk that ENDS this far (metres) from its destination ended blocked, not
// arrived. Above any sensible use-range/arrival distance so a successful
// walk-up never trips it. Read once by the cycle layer's way-blocked guard.
constexpr float kBlockedThresholdM = 4.0f;
bool g_walkBlocked = false;  // one-shot: last walk ended short of its target

// Arm in-flight tracking for a freshly dispatched move. Resets every latch so
// stale state from a prior walk can't end this one early.
void ArmInFlight(const Vector& dest) {
    DWORD now = GetTickCount();
    g_inFlight.active       = true;
    g_inFlight.dest         = dest;
    g_inFlight.dispatchTick = now;
    g_inFlight.sawPending   = false;
    g_inFlight.lastMoveTick = now;
    g_inFlight.sawMoving    = false;
    g_inFlight.haveLastPos  = false;
    g_walkBlocked           = false;  // fresh walk — clear any stale blocked flag
}

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

// CSWSObject.area_id — passed as objectId1 to AddMoveToPointAction.
constexpr size_t kServerObjectAreaIdOffset = 0x8c;

// CSWSCreature::ActionManager @0x004f8770 — primes the creature's action
// subsystem for the kind of action about to be queued (mode 8 = move/walk,
// 2 = use). Every engine handler that queues a player action calls this
// FIRST (the native click-to-move handler HandlePlayerToServerInputWalkToWaypoint
// does ActionManager(8) before AddMoveToPointAction). Without it the leader's
// queued move bails (field427 stays 2, no path) — this is the priming our
// earlier WalkTo dispatches were missing.
constexpr uintptr_t kAddrCSWSCreatureActionManager = 0x004f8770;
void PrimeActionManager(void* creature, int mode) {
    if (!creature) return;
    __try {
        auto fn = reinterpret_cast<void (__thiscall*)(void*, int)>(
            kAddrCSWSCreatureActionManager);
        fn(creature, mode);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Read an int field off a server object with SEH protection.
int ReadServerObjInt(void* obj, size_t off, int dflt) {
    if (!obj) return dflt;
    __try {
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(obj) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return dflt;
    }
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
    // PlayCue3D's pattern).
    Vector dest = destination;

    // Secondary point = zero, exactly as the native click-to-move handler
    // (HandlePlayerToServerInputWalkToWaypoint) passes it. With the action
    // manager primed (PrimeActionManager(8) below) this engages the engine's
    // nav-graph A* to the bare coordinate — around corners — instead of the
    // line-of-sight straight-move shortcut. (secondary=0 WITHOUT the priming
    // is what gave field427=2 / no movement earlier.)
    Vector secondary = {0.0f, 0.0f, 0.0f};

    // Snapshot the player's pre-dispatch position so the watchdog can
    // measure displacement-since-press. If this read fails we still
    // dispatch — the watchdog just logs without a baseline.
    Vector startPos = {0.0f, 0.0f, 0.0f};
    bool haveStart = acc::engine::GetPlayerPosition(startPos);

    unsigned int ret = 0;
    unsigned short thisActionId = s_actionId++;

    // Pass the player's real area id so cross-room pathfinding resolves. We
    // deliberately do NOT disable player input — that flips the client creature
    // mode (SwitchMode 0) and suppresses the walk (observed on the dialog path).
    unsigned long areaId = static_cast<unsigned long>(
        ReadServerObjInt(creature, kServerObjectAreaIdOffset,
                         static_cast<int>(kInvalidObjectId)));

    // Prime the action subsystem for a move BEFORE queuing it — the missing
    // step that made standalone WalkTo dispatches bail for the leader (the move
    // sat unprocessed, field427 stuck at 2). Mode 8 = move/walk, matching the
    // native click-to-move handler. This priming — not an ai_level change — is
    // what actually engages the pathfind for the player; the player's natural
    // ai_level is already non-zero, so no SetAILevel is needed.
    PrimeActionManager(creature, 8);

    __try {
        auto fn = reinterpret_cast<PFN_AddMoveToPointAction>(
            kAddrCSWSCreatureAddMoveToPointAction);
        ret = fn(creature,
           /*actionId=*/0xffff,        // native handler uses 0xffff, not a seq id
           &dest,
           areaId, kInvalidObjectId,   // objectId1 = area id; objectId2 = none
           /*runFlag=*/1,
           /*radius=*/0.0f,
           /*followDistance=*/0.0f,
           /*forceFlag=*/0,
           /*timeoutMs=*/0,
           /*pathMode=*/0,
           /*avoidFlags=*/0,
           /*flagBit3=*/0,
           /*flagBit9=*/0,
           &secondary,                 // (0,0,0) — skip the LOS direct-move
                                       // shortcut so the engine runs full A*
           /*pathContext1=*/0,
           /*pathContext2=*/0,
           /*flagBit10=*/0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Disarm watchdog — no point measuring progress when the call faulted.
        g_watchdog.active = false;
        acclog::Write("Autowalk", "WalkTo SEH-FAULT action_id=%u "
                      "dest=(%.2f,%.2f,%.2f)",
                      static_cast<unsigned>(thisActionId),
                      dest.x, dest.y, dest.z);
        return false;
    }

    // Arm the watchdog. New dispatch supersedes any prior in-flight
    // observation — the user pressed Shift+- again or a new caller fired,
    // so the prior baseline is no longer the relevant reference point.
    ArmWatchdog(startPos, haveStart, dest, "WalkTo");

    ArmInFlight(dest);

    float distToDest = haveStart ? HorizontalDistance(startPos, dest) : -1.0f;

    acclog::Write("Autowalk", "WalkTo dispatch dest=(%.2f,%.2f,%.2f) "
                  "from=(%.2f,%.2f,%.2f) dist=%.2fm action_id=%u "
                  "areaId=0x%08lx ret=0x%08x",
                  dest.x, dest.y, dest.z,
                  startPos.x, startPos.y, startPos.z,
                  distToDest,
                  static_cast<unsigned>(thisActionId),
                  areaId, ret);

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
        acclog::Write("Autowalk", "Force-dispatch SEH-FAULT action_id=%u "
                      "dest=(%.2f,%.2f,%.2f)",
                      action.action_id, dest.x, dest.y, dest.z);
        return false;
    }

    ArmWatchdog(startPos, haveStart, dest, "Force");

    g_inFlight.active = true;
    g_inFlight.dest   = dest;

    float distToDest = haveStart ? HorizontalDistance(startPos, dest) : -1.0f;
    acclog::Write("Autowalk", "Force-dispatch dest=(%.2f,%.2f,%.2f) "
                  "from=(%.2f,%.2f,%.2f) dist=%.2fm action_id=%u "
                  "(no ret — void)",
                  dest.x, dest.y, dest.z,
                  startPos.x, startPos.y, startPos.z,
                  distToDest, action.action_id);
    return true;
}

bool UseObject(unsigned long targetHandle, const Vector& destHint) {
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
        acclog::Write("Autowalk", "UseObject SEH-FAULT target=0x%08lx",
                      targetHandle);
        return false;
    }

    // Arm in-flight tracking when caller supplied a destination hint.
    // The shared TickProgressWatchdog clears the flag when the player
    // reaches within 1m, so cycle_input's Shift+- toggle-cancel sees
    // the same "in flight" state UseObject paths set as WalkTo paths.
    bool destValid = destHint.x != 0.0f || destHint.y != 0.0f ||
                     destHint.z != 0.0f;
    if (ret != 0 && destValid) {
        ArmInFlight(destHint);
    }

    acclog::Write("Autowalk", "UseObject dispatch target=0x%08lx ret=%d "
                  "destHint=(%.2f,%.2f,%.2f) inFlightArmed=%d",
                  targetHandle, ret,
                  destHint.x, destHint.y, destHint.z,
                  (ret != 0 && destValid) ? 1 : 0);
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
        acclog::Write("Autowalk", "CancelMovement SEH-FAULT");
    }

    // Clear local state regardless of engine call success — at minimum,
    // the user said "stop", so don't pretend we're still in flight. A user
    // cancel is NOT "blocked" — clear the flag so the cycle guard stays quiet.
    g_inFlight.active = false;
    g_watchdog.active = false;
    g_walkBlocked     = false;

    if (ok) {
        acclog::Write("Autowalk", "CancelMovement dispatched (ClearAllActions(0))");
    }
    return ok;
}

bool ConsumeWalkBlocked() {
    bool b = g_walkBlocked;
    g_walkBlocked = false;
    return b;
}

bool IsAutowalkInFlight() {
    return g_inFlight.active;
}

void PollMovementKeysCancel() {
    // Only cancel our own autowalks. Engine-initiated autorun (Canderous
    // recruitment dialog hand-off, area onEnter scripts, cutscene moves)
    // keeps `g_inFlight.active` false because we never set it for those —
    // so this gate preserves script-driven sequences from accidental
    // cancellation by stray W presses.
    if (!g_inFlight.active) return;

    // Foreground gate — GetAsyncKeyState reads OS-global state, so a W
    // press in another app while the user is Alt+Tabbed out would
    // otherwise cancel the in-flight autowalk silently. Match the
    // hotkeys-module convention here.
    if (!acc::hotkeys::IsForegroundGame()) return;

    // User's movement-key set on QWERTZ (German) layout. VK_W / VK_S /
    // VK_A / VK_D / VK_C / VK_Y map to the physical positions the user
    // listed. If their layout produces different VK codes for some of
    // these letters we'll see no cancel firing on the offending key and
    // can extend the list.
    constexpr int kMovementKeys[] = {'W', 'S', 'A', 'D', 'C', 'Y'};
    bool anyDown = false;
    for (int vk : kMovementKeys) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            anyDown = true;
            break;
        }
    }

    // Rising-edge gate. If the user happens to be holding W when an
    // autowalk dispatches (e.g. Shift+- then immediate W), we don't
    // want to cancel on tick 1 just because the key was already down —
    // wait for a fresh press. After cancel, g_inFlight.active flips to
    // false and the early-return at top of this function takes over;
    // s_prevDown stays accurate for the next dispatch.
    static bool s_prevDown = false;
    bool risingEdge = anyDown && !s_prevDown;
    s_prevDown = anyDown;
    if (!risingEdge) return;

    bool ok = CancelMovement();
    if (ok) {
        // Re-enable manual control immediately — the user wants the
        // keyboard back NOW, not after the 3s auto-restore. Same
        // sequence as the Shift+- toggle-cancel path in cycle_input.cpp.
        acc::engine::SetPlayerInputEnabled(true);
        const char* msg = acc::strings::Get(
            acc::strings::Id::MovementCancelled);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Autowalk", "movement-key cancel — %s rising edge",
                      "W/S/A/D/C/Y");
    } else {
        // CancelMovement SEH-faulted (logged inside that function). Drop
        // s_prevDown back to false so the next press still tries.
        s_prevDown = false;
    }
}

void TickProgressWatchdog() {
    // In-flight arrival check — runs even when the diagnostic watchdog has
    // self-disengaged. Cheap (one position read + horizontal-distance
    // compare) and only when actually in flight.
    if (g_inFlight.active) {
        Vector pos;
        bool   havePos = acc::engine::GetPlayerPosition(pos);
        int    depth   = acc::engine::GetPlayerActionQueueDepth();
        DWORD  now     = GetTickCount();
        bool   pastGrace = (now - g_inFlight.dispatchTick) >= kInFlightGraceMs;

        bool done = false;
        if (!havePos) {
            done = true;                                   // player gone
        } else if (HorizontalDistance(pos, g_inFlight.dest) < 1.0f) {
            done = true;                                   // arrived at the point
        } else {
            // Track movement: refresh the "last moved" stamp whenever the PC
            // displaces past the epsilon.
            if (!g_inFlight.haveLastPos) {
                g_inFlight.lastPos     = pos;
                g_inFlight.haveLastPos = true;
            } else {
                float dx = pos.x - g_inFlight.lastPos.x;
                float dy = pos.y - g_inFlight.lastPos.y;
                if (dx * dx + dy * dy >= kInFlightMoveEpsSq) {
                    g_inFlight.lastPos      = pos;
                    g_inFlight.lastMoveTick = now;
                    g_inFlight.sawMoving    = true;
                }
            }
            if (depth > 0) g_inFlight.sawPending = true;

            // Two completion signals, whichever fires first:
            //   (a) queue fully drained — the fast path for WalkTo (a single
            //       move action that completes / is dropped at a wall);
            //   (b) the PC has been motionless for kInFlightStillMs — the only
            //       reliable signal for UseObject, whose composite queue never
            //       drains to 0 (it oscillates through the walk and into the
            //       conversation it opens) but which still leaves the PC
            //       standing still once it reaches use-range.
            // Both gate on grace / having-seen-activity so the post-dispatch
            // ramp-up isn't mistaken for completion.
            bool drained = depth == 0 && (g_inFlight.sawPending || pastGrace);
            bool stalled = (g_inFlight.sawMoving || pastGrace) &&
                           (now - g_inFlight.lastMoveTick) >= kInFlightStillMs;
            if (drained || stalled) done = true;
        }

        if (done) {
            // Flag "ended blocked" for the cycle layer's way-blocked guard:
            // the walk finished on its own (NOT via CancelMovement, which
            // clears g_inFlight.active directly and never reaches here) but the
            // PC is still well short of the destination — stalled at a wall /
            // railing, or the engine dropped an unreachable move. A normal
            // arrival (within ~1m via the branch above, or use-range a few m
            // out) is under the threshold and not flagged.
            g_walkBlocked = havePos &&
                HorizontalDistance(pos, g_inFlight.dest) > kBlockedThresholdM;
            g_inFlight.active = false;
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

        // Diagnostic: read the same fields we sampled at dispatch so we
        // can see if AIActionMoveToPoint ran (and which branch).
        // field427_0xa8c value codes: 2=never ran, 1=switch case took it,
        // 0=short-tail or long-branch path, -1=long-branch reset.
        int32_t  curField427 = 0;
        uint32_t curField101 = 0;
        void* creature = acc::engine::GetPlayerServerCreature();
        if (creature) {
            __try {
                auto* base = reinterpret_cast<unsigned char*>(creature);
                curField427 = *reinterpret_cast<int32_t*>(base + 0xa8c);
                curField101 = *reinterpret_cast<uint32_t*>(base + 0x1f8);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (g_watchdog.haveStartPos) {
            float moved = HorizontalDistance(g_watchdog.startPos, pos);
            float distToDest = HorizontalDistance(pos, g_watchdog.dest);
            const char* state = (moved < 0.1f) ? "stuck" : "moving";
            acclog::Write("Autowalk", "%s t+1s moved=%.2fm dist=%.2fm (%s) "
                "field427=%d field101=0x%08x",
                g_watchdog.tag, moved, distToDest, state,
                curField427, static_cast<unsigned>(curField101));
        } else {
            // No baseline: still useful — we know whether we're near the
            // destination at the 1s mark.
            float distToDest = HorizontalDistance(pos, g_watchdog.dest);
            acclog::Write("Autowalk", "%s t+1s dist=%.2fm (no baseline) "
                "field427=%d field101=0x%08x",
                g_watchdog.tag, distToDest,
                curField427, static_cast<unsigned>(curField101));
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
            acclog::Write("Autowalk", "%s t+3s moved=%.2fm dist=%.2fm (%s)",
                          g_watchdog.tag, moved, distToDest, state);
        } else {
            const char* state = (distToDest < 1.0f) ? "reached" : "unknown";
            acclog::Write("Autowalk", "%s t+3s dist=%.2fm (%s, no baseline)",
                          g_watchdog.tag, distToDest, state);
        }
        // Disengage. Future user actions (next Shift+-, etc.) will re-arm.
        g_watchdog.active = false;
    }
}

}  // namespace acc::guidance
