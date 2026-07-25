#include "endar_softlock.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_area.h"     // GetCurrentAreaResName, ReadGlobalNumber, GetObjectTag
#include "engine_player.h"   // IsAnyPartyMemberInCombat
#include "locked_recall.h"   // IsGenericLockedMessage — the router seam we hang off
#include "log.h"
#include "msg_router.h"
#include "narrated_target.h" // the object the locked feedback refers to
#include "prism.h"
#include "strings.h"

namespace acc::endar {

namespace {

// Module resref of the Endar Spire Command Module (language-independent).
constexpr char kCommandModule[] = "end_m01aa";

// The west-corridor door to the bridge. Locked until room 5 is cleared; the
// only reported softlock chokepoint. Kept as a single tag rather than a set so
// the guidance stays precise — the other gated doors (end_door08/15) sit past
// this one and are never reached while it's shut.
constexpr char kRoom5Door[] = "end_door16";

// The door beside the doomed "cut2" battle. Its blueprint is Locked=0 with an
// empty OnFailToOpen, and opening it is what fires k_pend_room5_02 — the script
// that runs the scripted battle and the Sith reinforcement wave behind it. So
// this line must NEVER be spoken in place of an open attempt (v0.6.1 did that
// and softlocked the tutorial); it only fires if the engine itself reports the
// door as locked, which on a healthy playthrough never happens.
constexpr char kCut2Door[] = "end_door10_cut2";

// The Trask door. Locked=1 from area load with OnFailToOpen wired to
// k_pend_traskdie1, so the engine reports it locked at TWO different points and
// only the second one means "sealed":
//   * before the sacrifice — the failed open IS the story trigger. It makes
//     Trask demand you level up first, then plays his death scene. Announcing
//     "will not open" here would send the player away from the one door that
//     advances the plot: the same harm the old interact-site seal did, just
//     delivered as wrong words instead of a suppressed engine call.
//   * after the sacrifice — Trask has gone through it and it never opens again.
//     The way on is the Starboard Deck, and the line is exactly right.
// Gate: Trask is the only companion in END_M01AA, so an empty companion roster
// means his scene has played. Confirmed in patch-20260724-232609.log — the door
// read unlocked at 23:34:01 (the open ran the sacrifice dialogue) and locked on
// every attempt from 23:34:22 on.
constexpr char kTraskDoor[] = "end_door19";

// Failed opens on the room-5 door before we escalate from "the door opens
// after the fight" to "you may be stuck, reload". A normal player pokes it
// once or twice on the way to clearing the room; this many repeats means
// there's nothing left to do.
constexpr int kReloadHintAfterAttempts = 3;

// Delay the spoken hint a beat so it follows the engine's own "locked" bark
// rather than racing it.
constexpr DWORD kHintDelayMs = 500;

// Re-scan the plot globals at most this often for the diagnostic edge-log.
constexpr DWORD kDiagScanIntervalMs = 500;

bool  g_inModule        = false;
int   g_door16Attempts  = 0;
bool  g_spokeBattleHint = false;
bool  g_spokeReloadHint = false;

bool                  g_hasPendingHint = false;
acc::strings::Id      g_pendingHint    = acc::strings::Id::EndarDoorBattleHint;
DWORD                 g_pendingHintAtMs = 0;

DWORD g_lastDiagScanMs = 0;

// Last edge-logged plot snapshot. -2 = never logged (forces a first emit).
struct Snapshot {
    int room3 = -2, room5 = -2, room7 = -2, room8 = -2, sith = -2;
    int bridgeCombat = -2, inCombat = -2;
    bool operator==(const Snapshot& o) const {
        return room3 == o.room3 && room5 == o.room5 && room7 == o.room7 &&
               room8 == o.room8 && sith == o.sith &&
               bridgeCombat == o.bridgeCombat && inCombat == o.inCombat;
    }
};
Snapshot g_lastSnap;

bool EqualsCI(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return *a == *b;
}

bool IsCommandModule() {
    char res[64] = "";
    return acc::engine::GetCurrentAreaResName(res, sizeof(res)) &&
           EqualsCI(res, kCommandModule);
}

Snapshot ReadSnapshot() {
    using acc::engine::ReadGlobalNumber;
    Snapshot s;
    s.room3        = ReadGlobalNumber("END_ROOM3_DEAD");
    s.room5        = ReadGlobalNumber("END_ROOM5_DEAD");
    s.room7        = ReadGlobalNumber("END_ROOM7_DEAD");
    s.room8        = ReadGlobalNumber("END_ROOM8_DEAD");
    s.sith         = ReadGlobalNumber("END_SITH_DEAD");
    s.bridgeCombat = ReadGlobalNumber("END_BRIDGE_COMBAT");
    s.inCombat     = acc::engine::IsAnyPartyMemberInCombat() ? 1 : 0;
    return s;
}

void LogSnapshot(const char* why, const Snapshot& s) {
    acclog::Write("Endar.Diag",
        "%s room3=%d room5=%d room7=%d room8=%d sith=%d bridgeCombat=%d "
        "inCombat=%d door16Tries=%d",
        why, s.room3, s.room5, s.room7, s.room8, s.sith, s.bridgeCombat,
        s.inCombat, g_door16Attempts);
}

void ResetVisit() {
    g_door16Attempts  = 0;
    g_spokeBattleHint = false;
    g_spokeReloadHint = false;
    g_hasPendingHint  = false;
    g_lastSnap        = Snapshot{};
}

// Active companions, PC excluded (the engine's party table counts followers
// only). 0 while Trask is gone; 0 is also what the accessor returns on an
// early-init/fault read, which can't coincide with the player pressing Enter on
// a door in a live world.
int CompanionCount() {
    uint32_t handles[8] = {};
    return acc::engine::GetPartyMembers(handles, 8);
}

// Queue a line to be spoken after the generic "locked" the router is about to
// emit. Rules run before the router speaks an unclaimed line, so speaking
// inline would land ahead of it; deferring a beat keeps the order
// "locked" -> explanation.
void QueueHint(acc::strings::Id which) {
    g_pendingHint     = which;
    g_hasPendingHint  = true;
    g_pendingHintAtMs = GetTickCount() + kHintDelayMs;
}

// The engine just reported `tag` as locked. Speak the area-specific reason
// where we have one. Cheap no-op for every other tag and every other module.
void OnDoorLocked(const char* tag) {
    if (!tag || !tag[0] || !IsCommandModule()) return;

    // The cutscene-battle door. Blueprint-unlocked, so reaching here at all
    // means the engine really did refuse the open — say so plainly instead of
    // leaving the generic "locked" reading like a lock that could be picked.
    if (EqualsCI(tag, kCut2Door)) {
        QueueHint(acc::strings::Id::DoorSealedNoOpen);
        acclog::Write("Endar.Softlock",
            "%s reported locked -> queue sealed line", kCut2Door);
        return;
    }

    // The Trask door — only sealed once he is gone. See kTraskDoor.
    if (EqualsCI(tag, kTraskDoor)) {
        int companions = CompanionCount();
        if (companions > 0) {
            acclog::Write("Endar.Softlock",
                "%s reported locked, but %d companion(s) still in the party — "
                "the level-up gate / sacrifice is still ahead, staying quiet",
                kTraskDoor, companions);
            return;
        }
        QueueHint(acc::strings::Id::DoorSealedNoOpen);
        acclog::Write("Endar.Softlock",
            "%s reported locked with an empty companion roster (Trask gone) "
            "-> queue sealed line", kTraskDoor);
        return;
    }

    if (!EqualsCI(tag, kRoom5Door)) return;

    // room 5 cleared -> the door is (or will be) open; no guidance needed.
    int room5 = acc::engine::ReadGlobalNumber("END_ROOM5_DEAD");
    if (room5 > 0) return;

    ++g_door16Attempts;

    acc::strings::Id which;
    if (g_door16Attempts >= kReloadHintAfterAttempts && !g_spokeReloadHint) {
        which = acc::strings::Id::EndarStuckReloadHint;
        g_spokeReloadHint = true;
    } else if (!g_spokeBattleHint) {
        which = acc::strings::Id::EndarDoorBattleHint;
        g_spokeBattleHint = true;
    } else {
        acclog::Write("Endar.Softlock",
            "end_door16 try #%d (room5=0) — hint already spoken this visit",
            g_door16Attempts);
        return;
    }

    QueueHint(which);
    acclog::Write("Endar.Softlock",
        "end_door16 try #%d (room5=0) -> queue hint id=%d",
        g_door16Attempts, static_cast<int>(which));
}

// Feedback-router rule. Never consumes: the engine's generic "locked" still
// speaks through the router's default path and our explanation follows it.
// Runs for every locked report in the game; self-gates on module + tag.
bool RuleEndarDoorLocked(const char* text) {
    if (!acc::locked_recall::IsGenericLockedMessage(text)) return false;

    // The locked feedback carries no object id — the object is whatever the
    // player just acted on, i.e. the narrated-target slot the interact used.
    acc::narrated_target::Slot slot;
    if (!acc::narrated_target::TryGet(slot) || slot.isMapPin || !slot.obj) {
        return false;
    }

    char tag[64] = "";
    if (acc::engine::GetObjectTag(slot.obj, tag, sizeof(tag))) {
        OnDoorLocked(tag);
    }
    return false;
}

}  // namespace

void RegisterMsgRule() {
    acc::msg::Router::Instance().AddRule("EndarDoorLocked", RuleEndarDoorLocked);
}

void OnAreaChanged(void* /*area*/) {
    bool nowIn = IsCommandModule();
    if (nowIn && !g_inModule) {
        ResetVisit();
        Snapshot s = ReadSnapshot();
        g_lastSnap = s;
        LogSnapshot("enter END_M01AA:", s);
    } else if (!nowIn && g_inModule) {
        ResetVisit();
    }
    g_inModule = nowIn;
}

void Tick() {
    // Flush a queued hint once its delay elapses (follows the locked bark).
    if (g_hasPendingHint && GetTickCount() >= g_pendingHintAtMs) {
        const char* line = acc::strings::Get(g_pendingHint);
        prism::Speak(line, /*interrupt=*/false);
        acclog::Write("Endar.Softlock", "speak hint -> [%s]", line);
        g_hasPendingHint = false;
    }

    // Diagnostic edge-log while in the module (throttled).
    DWORD now = GetTickCount();
    if (now - g_lastDiagScanMs < kDiagScanIntervalMs) return;
    g_lastDiagScanMs = now;

    if (!g_inModule) return;
    Snapshot s = ReadSnapshot();
    if (!(s == g_lastSnap)) {
        LogSnapshot("change:", s);
        g_lastSnap = s;
    }
}

}  // namespace acc::endar
