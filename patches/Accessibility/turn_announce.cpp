#include "turn_announce.h"

#include <windows.h>
#include <cmath>

#include "engine_compass.h"
#include "engine_offsets.h"  // kCreatureCombatRoundOffset etc — combat-round
                             // probe added 2026-05-27 (third session) to
                             // capture engine combat-state + queued action
                             // count at every spin announce. ml=0 ruled out
                             // mouse-look; next-most-likely path is an
                             // engine-queued action driving the rotation
                             // (auto-attack on perceived hostile,
                             // ActionStartConversation, ActionRunToObject).
#include "engine_options.h"  // GetMouseLook — strong candidate for the spin
                             // bug. Mouse Look ON auto-rotates the player
                             // toward the screen-cursor world-ray every frame;
                             // with cursor parked at screen-centre and many
                             // NPCs in arc, each rotation moves the camera,
                             // which moves the world-ray, which picks a
                             // different rotation target. Logging the bit at
                             // every TurnAnnounce confirms or refutes.
#include "engine_player.h"
#include "hotkeys.h"  // IsForegroundGame — diagnostic only. We log fg state on
                      // every announce so we can correlate spin bursts with
                      // background-window state, but speech still fires
                      // unconditionally: the user must hear the bug to know
                      // it's happening.
#include "log.h"
#include "strings.h"
#include "prism.h"

namespace acc::turn_announce {

namespace {

// Sector geometry — shared with camera_announce. Compass frame
// (0° = North, CW positive), 8 × 45° wedges. The conversion math lives
// in engine_compass.{h,cpp}; only the hysteresis state stays here.
//
// Hysteresis: once a sector is active, leaving it requires the yaw to
// exceed the strict boundary by an additional 5° (per the long-term
// plan §"Locked defaults — Pillar 2"). Prevents border-thrashing
// announcements when the player parks near a sector boundary and
// rotates micro-amounts.
constexpr float kSectorSize  = 45.0f;   // 360 / 8
constexpr float kHalfSector  = 22.5f;   // strict boundary
constexpr float kHysteresis  = 5.0f;    // sticky boundary = 22.5 + 5

// Final-state debounce: only speak after the sector has been stable for
// this many ms. Collapses bursts like W→S (character spins 180° across
// 4 sectors in <1s) to a single announcement of the final direction.
constexpr DWORD kQuietMs = 250;

// Compute the smallest signed angular difference (a - b), normalized
// to (-180, +180]. Used for hysteresis — measures "how far is the
// current yaw from the centre of the active sector".
float AngularDelta(float a, float b) {
    float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
    return d;  // in (-180, +180]
}

// Probe CSWPlayerControl.enabled @+0xc — engine.h:162 documents this as the
// per-tick movement-clobber gate. When enabled=0 the input loop skips the
// W/A/S/D-driven movement override and the AI scheduler processes the
// creature instead. If a stray disable leaves the leader running AI we'd
// see exactly the symptom: user holds W, but the leader rotates and walks
// per AI behaviour, not per input. Returns 1/0 on success, -1 on chain
// failure (no app, no player_control, SEH fault).
int ReadPlayerControlEnabled() {
    constexpr uintptr_t kAppManagerPtr = kAddrAppManagerPtr;
    __try {
        void* appManager = *reinterpret_cast<void**>(kAppManagerPtr);
        if (!appManager) return -1;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return -1;
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
        if (!internal) return -1;
        void* playerControl = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kClientAppPlayerControlOffset);
        if (!playerControl) return -1;
        int enabled = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(playerControl) + 0xc);
        return enabled ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// CSWSObject.action_nodes outer queue probe. Per docs/combat-system.md:917
// the general (non-combat) action queue lives at CSWSObject+0xfc — this
// is what holds ActionMoveToPoint / ActionStartConversation /
// ActionUseObject etc. The combat round queue at +0x9b0 only sees
// combat-specific actions (attacks, force powers, items). The 2026-05-27
// session 3 log showed cb=1 eng=0 q=0 during the spin → combat round
// is innocent; if anything is queueing rotations, it's on this outer
// queue.
//
// Layout assumption: same shape as combat_round.actions — CExoLinkedList
// at the offset (either inline or pointer; we treat +0xfc as the head
// pointer and walk from there). SEH-guarded so a wrong assumption fails
// silently (outDepth=-1 marks a read failure distinct from "empty").
int ReadOuterQueueDepth(void* creature) {
    if (!creature) return -1;
    __try {
        // Try-as-pointer first: *(creature + 0xfc) → list head pointer.
        void* head = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(creature) + 0xfc);
        int count = 0;
        int walked = 0;
        void* node = head;
        while (node && walked < 32) {
            ++walked;
            // CExoLinkedList node: data ptr at +0, next ptr at +4
            // (matches the kLinkedListNodeData/Next layout used by
            // combat_queue.cpp).
            void* data = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kLinkedListNodeDataOff);
            if (data) ++count;
            node = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kLinkedListNodeNextOff);
        }
        return count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Combat-round probe: read engaged + current_action + queue depth from
// the leader's combat round. Returns true on a clean read; on any SEH
// fault or null link, returns false and leaves outputs zeroed. Used to
// distinguish "engine thinks we're in combat and auto-faces targets"
// (engaged=1) from "non-combat reorientation source" (engaged=0).
//
// All three reads are __try-guarded — combat_round is a per-creature
// field that's only valid on creatures; if someone changes the leader
// chain underneath us mid-tick this would otherwise faultfast.
bool ReadCombatProbe(int& outEngaged, int& outCurrentAction,
                     int& outQueueDepth) {
    outEngaged = 0;
    outCurrentAction = 0;
    outQueueDepth = 0;

    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) return false;

    __try {
        void* round = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(creature) +
            kCreatureCombatRoundOffset);
        if (!round) return false;

        outEngaged = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(round) +
            kCombatRoundEngagedOffset);
        outCurrentAction = static_cast<int>(
            *reinterpret_cast<unsigned char*>(
                reinterpret_cast<unsigned char*>(round) +
                kCombatRoundCurrentActionOffset));

        // Count queued action nodes — skip the type=0xFF placeholder
        // the engine maintains as the current-dispatching slot.
        void* listPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(round) +
            kCombatRoundActionsOffset);
        if (listPtr) {
            void* node = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(listPtr) +
                kLinkedListHeadOffset);
            int walked = 0;
            while (node && walked < 32) {
                ++walked;
                void* data = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(node) +
                    kLinkedListNodeDataOff);
                if (data) {
                    unsigned char t = *(reinterpret_cast<unsigned char*>(data)
                                        + kCombatRoundActionTypeOffset);
                    if (t != 0xFF) ++outQueueDepth;
                }
                node = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(node) +
                    kLinkedListNodeNextOff);
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void Tick() {
    float engineYawDeg = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(engineYawDeg)) return;

    float compass = acc::engine::EngineYawToCompass(engineYawDeg);
    DWORD now = GetTickCount();

    // Foreground gate + position delta + mouse-look bit — diagnostic for
    // the "engine rotates character while player holds W" report
    // (2026-05-27). Sample position alongside yaw so we can distinguish
    // "engine spinning a stationary character" from "user walking + engine
    // rotating during collision response". Mouse-look bit added 2026-05-27
    // (second session): if mouse_look=1 here, the engine is auto-facing the
    // player toward the cursor-ray world hit every frame — a self-
    // reinforcing oscillation with the camera as multiplier.
    bool isForeground = acc::hotkeys::IsForegroundGame();
    bool mouseLook    = false;
    bool mouseLookOk  = acc::engine::GetMouseLook(mouseLook);
    int  cbEngaged = 0, cbCurrent = 0, cbQueue = 0;
    bool cbOk = ReadCombatProbe(cbEngaged, cbCurrent, cbQueue);
    int  outerQ = ReadOuterQueueDepth(
        acc::engine::GetPlayerServerCreature());
    int  pcEnabled = ReadPlayerControlEnabled();
    Vector pos{};
    bool havePos = acc::engine::GetPlayerPosition(pos);
    static Vector s_lastPos = {0.0f, 0.0f, 0.0f};
    static bool   s_havePrevPos = false;
    float dposX = havePos && s_havePrevPos ? (pos.x - s_lastPos.x) : 0.0f;
    float dposY = havePos && s_havePrevPos ? (pos.y - s_lastPos.y) : 0.0f;
    if (havePos) { s_lastPos = pos; s_havePrevPos = true; }

    // -1 = "first observation since DLL load — set sector but don't speak".
    static int   s_lastSpokenSector = -1;
    static int   s_pendingSector    = -1;
    static DWORD s_lastChangeAt     = 0;

    if (s_lastSpokenSector < 0) {
        s_lastSpokenSector = acc::engine::CompassToSector(compass);
        s_pendingSector    = s_lastSpokenSector;
        s_lastChangeAt     = now;
        acclog::Write("TurnAnnounce", "first-tick suppress; engineYaw=%.1f compass=%.1f "
            "sector=%d", engineYawDeg, compass, s_lastSpokenSector);
        return;
    }

    // Hysteresis around last *spoken* sector: while within (kHalfSector +
    // kHysteresis)° of its centre, treat current sector as unchanged.
    // Outside the band, compute the strict nearest sector.
    float lastCentre   = s_lastSpokenSector * kSectorSize;
    float distFromLast = std::fabs(AngularDelta(compass, lastCentre));
    int   currentSector = (distFromLast <= kHalfSector + kHysteresis)
                              ? s_lastSpokenSector
                              : acc::engine::CompassToSector(compass);

    // Track most-recent-observed sector + when it last changed. While the
    // player is mid-turn (e.g. W→S 180° spin), this fires every ~50ms and
    // keeps last_change_at moving — no announcement until the spin ends.
    if (currentSector != s_pendingSector) {
        s_pendingSector = currentSector;
        s_lastChangeAt  = now;
    }

    if (s_pendingSector == s_lastSpokenSector) return;
    if (now - s_lastChangeAt < kQuietMs)        return;  // not stable yet

    auto id = acc::engine::SectorString(s_pendingSector);
    const char* phrase = acc::strings::Get(id);

    // Urgent SAPI channel — a sector flip mid-spin survives NVDA's
    // typed-char-cancel, which used to swallow this announce while the
    // user was holding A/D. Voice 0 keeps the cue on the default urgent
    // voice; we tried alternate voices to distinguish it from other
    // urgent cues, but on this user's NVDA-via-SAPI bridge any non-0
    // voice either folded back to NVDA or sounded indistinguishable.
    // Single shared voice is the current preference; revisit if the
    // urgent-cue surface grows enough to warrant differentiation.
    prism::SpeakUrgent(phrase, /*voiceId=*/0);

    acclog::Write("TurnAnnounce", "sector %d -> %d (%s); engineYaw=%.1f compass=%.1f "
        "(debounced %ums fg=%d ml=%d pos=(%.2f,%.2f) dpos=(%+.3f,%+.3f) "
        "cb=%d eng=%d ca=%d q=%d oq=%d pc=%d)",
        s_lastSpokenSector, s_pendingSector, phrase, engineYawDeg, compass,
        static_cast<unsigned>(now - s_lastChangeAt),
        isForeground ? 1 : 0,
        mouseLookOk ? (mouseLook ? 1 : 0) : -1,
        pos.x, pos.y, dposX, dposY,
        cbOk ? 1 : 0, cbEngaged, cbCurrent, cbQueue, outerQ, pcEnabled);

    s_lastSpokenSector = s_pendingSector;
}

}  // namespace acc::turn_announce
