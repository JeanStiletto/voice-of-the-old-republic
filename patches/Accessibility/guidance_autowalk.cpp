#include "guidance_autowalk.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "camera_orient.h"  // IsActive — its snap-turn must not self-trip the
                            // movement-key cancel.
#include "engine_keymap.h"  // AnyMovementKeyHeld — the player's bound move/turn
                            // keys, so cancel works regardless of rebinds.
#include "engine_player.h"
#include "guidance_approach.h"  // IsAnyModApproachInFlight / CancelByMovement —
                                // the unified tracker owns in-flight semantics
                                // and the owner-aware cancel teardown.
#include "hotkeys.h"  // IsForegroundGame — gate movement-key cancel polling
                      // so keys pressed in another app while Alt+Tabbed out
                      // don't kill an in-flight autowalk silently.
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

// Walk dispatch is now a pure primitive: WalkTo / ForceWalkTo / UseObject queue
// the engine action and return. Watching whether the walk arrives, settles, or
// stalls — and the toggle-cancel / way-blocked semantics that used to live here
// as g_inFlight / g_watchdog / g_walkBlocked — is the unified approach tracker's
// job (guidance_approach.{h,cpp}). Callers arm it after a successful dispatch.

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
        acclog::Write("Autowalk", "WalkTo SEH-FAULT action_id=%u "
                      "dest=(%.2f,%.2f,%.2f)",
                      static_cast<unsigned>(thisActionId),
                      dest.x, dest.y, dest.z);
        return false;
    }

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
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        acclog::Write("Autowalk", "Force-dispatch SEH-FAULT action_id=%u "
                      "dest=(%.2f,%.2f,%.2f)",
                      action.action_id, dest.x, dest.y, dest.z);
        return false;
    }

    float distToDest = haveStart ? HorizontalDistance(startPos, dest) : -1.0f;
    acclog::Write("Autowalk", "Force-dispatch dest=(%.2f,%.2f,%.2f) "
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
        acclog::Write("Autowalk", "UseObject SEH-FAULT target=0x%08lx",
                      targetHandle);
        return false;
    }

    acclog::Write("Autowalk", "UseObject dispatch target=0x%08lx ret=%d",
                  targetHandle, ret);
    return ret != 0;
}

bool CancelMovement() {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) return false;

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

    if (ok) {
        acclog::Write("Autowalk", "CancelMovement dispatched (ClearAllActions(0))");
    }
    return ok;
}

void PollMovementKeysCancel() {
    // Cancel ANY mod-armed walk — Shift+- discovery (Cycle) or Enter interact
    // (Interact) — on a movement key. Engine-initiated movement (Canderous
    // recruitment hand-off, area onEnter scripts, cutscene moves) never arms the
    // tracker, so it is structurally unreachable here: we only ever cancel a walk
    // we ourselves dispatched.
    if (!acc::guidance::IsAnyModApproachInFlight()) return;

    // Foreground gate — GetAsyncKeyState reads OS-global state, so a movement
    // key pressed in another app while the user is Alt+Tabbed out would
    // otherwise cancel the in-flight autowalk silently.
    if (!acc::hotkeys::IsForegroundGame()) return;

    // camera_orient's snap-turn drives the camera by SendInput'ing A/D scancodes,
    // which GetAsyncKeyState reports as real presses. Don't let that self-cancel
    // an in-flight walk while the auto-rotation is running.
    if (acc::camera_orient::IsActive()) return;

    // Any of the player's bound movement / turn keys held cancels the walk —
    // read from swkotor.ini [Keymapping] (engine_keymap) so this follows a
    // rebind instead of assuming A/D/W/S, and still covers the legacy German
    // QWERTZ extras (C/Y/Z) as a union so it never regresses.
    //
    // Level-triggered (no rising-edge requirement): a key already held when the
    // walk dispatched still cancels, so the user can always turn/walk their way
    // out. The arm grace that prevents a pre-dispatch key from cancelling too
    // early lives in CancelByMovement (kCancelGraceMs), keyed off the tracker's
    // arm time.
    if (!acc::engine_keymap::AnyMovementKeyHeld()) return;

    acc::guidance::CancelByMovement();
}


}  // namespace acc::guidance
