#include "combat_special_watch.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>  // snprintf — tag formatting for per-creature queue dump

#include "audio_bus.h"        // PlayCue (2D one-shot)
#include "combat.h"           // IsCombatActive
#include "engine_area.h"      // ResolveServerObjectHandle, GetObjectKind,
                              // GameObjectKind
#include "engine_offsets.h"
#include "engine_player.h"    // GetPartyMembers, GetCameraPosition
#include "log.h"
#include "transitions.h"      // IsModuleLoadPending — gate during cutscene-load
                              // transient (engine LYT loader use-after-free)

namespace acc::combat::special_watch {

namespace {

// "You can act now" tactical cue. Tried gui_actqueue (too soft) and
// cb_gr_boncehard2 (still masked under heavy combat). Now trying
// c_drdastro_hit2 — astromech droid hit, a sharp metallic clang
// likely mastered hotter than the grenade-bounce family.
constexpr const char* kCueResref = "c_drdastro_hit2";

// Priority-group bucket. CExoSoundSourceInternal::SetPriorityGroup
// indexes into the engine's priority_groups[] table — each entry
// carries its OWN volume scalar (0-127), pitch variance, and 3D
// falloff. Probed live 2026-05-14 (see probe_priority_groups.cpp):
//
//   group 0    vol=106  (default — what cues used to sit on)
//   group 3,6,7,10-17  vol=127 (max byte volume)
//   group 14,15        vol=127 + variance=0.3 (weapon swings)
//   group 24           vol=127 (priority=10, max_dist=100)
//
// We pick 15 — same group weapon swings use, vol=127, sits in the
// "tactical combat event" tier the engine itself uses for melee
// SFX. Should compete on equal footing with combat audio for voice
// slots.
constexpr uint8_t kCuePriorityGroup = 15;

// Per-source volume byte on top of the priority-group bus. 127 is
// the max byte; combined with kCuePriorityGroup's vol=127 this
// maxes the source side of the gain chain. Final loudness depends
// on slider × bias_2d3d × playback path (2D linear vs 3D
// compressor).
constexpr uint8_t kCueVolumeByte = 127;

// 2D playback. The 3D-at-listener trick gave a small (+3-6 dB)
// perceived boost via the engine's pow(x, 0.6) compressor but read
// as an overcomplicated quirk-exploit. Reverted to clean 2D — same
// API the original nav cues use, no positional semantics, no
// listener bias gymnastics.
constexpr bool kCuePlayAs3D = false;

// First-round gate. No heartbeat fires during the first ~6s after
// combat entry so the existing "Kampf beginnt" announcement gets clean
// air. Tuned to match KOTOR's 6s round length.
constexpr DWORD kFirstRoundQuietMs = 6000;

// Repeat cadence. One combat round at the default tuning.
constexpr DWORD kRepeatPeriodMs = 6000;

// CSWSCombatRoundAction layout — only the fields we need beyond
// engine_offsets.h's published ones. Per Lane's type DB (entry
// CSWSCombatRoundAction):
//   action_type   @+0x10  byte
//   target        @+0x14  ulong
//   attack_feat?  @+0x84  ulong  (feat ID for Power Attack / Flurry /
//                                 Critical Strike; 0 for raw attacks)
constexpr size_t kActionAttackFeatOffset = 0x84;

// Read CSWSCreature.combat_round @+0x9c8. SEH-guarded.
void* ReadCombatRound(void* serverCreature) {
    if (!serverCreature) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureCombatRoundOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// True if `action` represents a routine auto-attack that the engine
// would have queued itself once combat is engaged. Anything else
// (feats, specials, casts, item-uses, equips, bashes on objects) is
// what the player would care to be reminded about. All field reads
// are __try-wrapped; on fault we treat the action as "special" (fail
// safe — better to silence than over-fire when state is corrupt).
bool IsRoutineAutoAttack(void* action) {
    if (!action) return false;
    unsigned char type = 0;
    uint32_t target = 0;
    uint32_t feat = 0;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(action);
        type   = *(base + kCombatRoundActionTypeOffset);
        target = *reinterpret_cast<uint32_t*>(
            base + kCombatRoundActionTargetOffset);
        feat   = *reinterpret_cast<uint32_t*>(
            base + kActionAttackFeatOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (type != 1) return false;          // any non-attack is special
    if (feat != 0) return false;          // feat-driven attack is special
    if (target == 0u || target == 0xFFFFFFFFu ||
        target == 0x7F000000u) {
        // No target / sentinel handle — treat as special so we don't
        // mistake malformed state for routine auto-attack.
        return false;
    }
    void* obj = acc::engine::ResolveServerObjectHandle(target);
    if (!obj) {
        obj = acc::engine::ResolveClientObjectHandle(target);
    }
    if (!obj) return false;
    int kind = acc::engine::GetObjectKind(obj);
    return kind == static_cast<int>(acc::engine::GameObjectKind::Creature);
}

// Walk one creature's combat_round.actions, returning the count of
// "special" actions (everything that ISN'T a routine auto-attack
// against a hostile creature). Filters the engine's 0xFF placeholder
// head node the same way combat_queue.cpp does.
//
// Diagnostic: when the walk encounters ANY non-placeholder item (even a
// routine auto-attack), emit a per-item log line. This is intentionally
// noisy — gives us per-tick visibility into the queue while we're
// chasing the "bare 1-7 dispatch produces no special" bug. Remove or
// gate behind a verbosity flag once the dispatch is understood.
int CountSpecialsForCreature(void* creature, const char* tag) {
    if (!creature) return 0;
    void* round = ReadCombatRound(creature);
    if (!round) return 0;
    int specials = 0;
    int rawItems = 0;
    __try {
        void* listPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(round) +
            kCombatRoundActionsOffset);
        if (!listPtr) return 0;
        void* node = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(listPtr) +
            kLinkedListHeadOffset);
        constexpr int kMaxWalk = 64;
        int walked = 0;
        while (node && walked < kMaxWalk) {
            ++walked;
            void* data = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kLinkedListNodeDataOff);
            if (data) {
                auto* base = reinterpret_cast<unsigned char*>(data);
                unsigned char t = *(base +
                    kCombatRoundActionTypeOffset);
                if (t != 0xFF) {
                    uint32_t target = *reinterpret_cast<uint32_t*>(
                        base + kCombatRoundActionTargetOffset);
                    uint32_t feat = *reinterpret_cast<uint32_t*>(
                        base + kActionAttackFeatOffset);
                    bool routine = IsRoutineAutoAttack(data);
                    acclog::Write("Combat.QueueRaw",
                        "[%s] item[%d] type=0x%02x target=0x%08x "
                        "feat=0x%08x routine=%d",
                        tag ? tag : "?", rawItems,
                        (unsigned)t, target, feat,
                        routine ? 1 : 0);
                    if (!routine) ++specials;
                    ++rawItems;
                }
            }
            node = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kLinkedListNodeNextOff);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return specials;
    }
    return specials;
}

// Party-wide specials count. 0 means every party member is on routine
// auto-attack (or has nothing queued at all); >0 means at least one
// member has a player-decided action pending.
int CountPartySpecials() {
    uint32_t handles[kPartyTableMaxMembers] = {};
    int n = acc::engine::GetPartyMembers(handles, kPartyTableMaxMembers);
    int total = 0;
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            void* c = acc::engine::ResolveServerObjectHandle(handles[i]);
            if (!c) c = acc::engine::ResolveClientObjectHandle(handles[i]);
            if (c) {
                char tag[16];
                std::snprintf(tag, sizeof(tag), "p%d", i);
                total += CountSpecialsForCreature(c, tag);
            }
        }
        return total;
    }
    // Fallback: party table unreadable, count the controlled creature
    // alone. Same shape as combat_queue.cpp's BuildRows fallback.
    void* leader = acc::engine::GetPlayerServerCreature();
    if (leader) total += CountSpecialsForCreature(leader, "leader");
    return total;
}

// State machine. All fields are static-local to this TU; the watcher
// owns its own state and resets cleanly across combat boundaries.
struct State {
    bool   inCombatPrev   = false;
    int    specialsPrev   = 0;
    DWORD  combatEnteredAt = 0;
    DWORD  lastTickAt     = 0;  // last time we played the cue
};

State& GetState() {
    static State s;
    return s;
}

void ResetForExit(State& s) {
    s.inCombatPrev   = false;
    s.specialsPrev   = 0;
    s.combatEnteredAt = 0;
    s.lastTickAt     = 0;
}

void FireCue(const char* reason, int specials, DWORD now) {
    if (kCuePlayAs3D) {
        Vector camera{};
        if (acc::engine::GetCameraPosition(camera)) {
            // Position the source AT the listener — the engine's 3D
            // path applies a pow(x, 0.6) compressor that boosts
            // perceived loudness over the linear 2D path; distance=0
            // keeps it "centred" so the cue doesn't drift directionally.
            acc::audio::PlayCue3D(kCueResref, camera, /*volume=*/127.0f);
        } else {
            // No camera (early init / between-area) — fall through to
            // 2D so we still fire rather than going silent.
            acc::audio::PlayCue(kCueResref, kCuePriorityGroup,
                                kCueVolumeByte);
        }
    } else {
        acc::audio::PlayCue(kCueResref, kCuePriorityGroup, kCueVolumeByte);
    }
    GetState().lastTickAt = now;
    acclog::Write("Combat.SpecialWatch",
                  "tick reason=%s specials=%d priority=%u vol=%u 3d=%d",
                  reason, specials, (unsigned)kCuePriorityGroup,
                  (unsigned)kCueVolumeByte, kCuePlayAs3D ? 1 : 0);
}

}  // namespace

void Tick() {
    // Module-load latch — IsCombatActive + per-creature queue walks read
    // engine accessors that aren't safe through a cutscene transition.
    if (acc::transitions::IsModuleLoadPending()) return;

    State& s = GetState();
    bool inCombatNow = acc::combat::IsCombatActive();
    DWORD now = GetTickCount();

    if (!inCombatNow) {
        if (s.inCombatPrev) {
            acclog::Write("Combat.SpecialWatch", "combat exit — reset");
            ResetForExit(s);
        }
        return;
    }

    // Combat-entry edge.
    if (!s.inCombatPrev) {
        s.inCombatPrev   = true;
        s.combatEnteredAt = now;
        s.specialsPrev    = CountPartySpecials();
        s.lastTickAt      = 0;  // never fired this combat yet
        acclog::Write("Combat.SpecialWatch",
                      "combat entered — specials=%d, first-round gate %lums",
                      s.specialsPrev, kFirstRoundQuietMs);
        return;
    }

    // First-round gate.
    if (now - s.combatEnteredAt < kFirstRoundQuietMs) {
        // Still keep specialsPrev fresh so the post-gate edge detector
        // doesn't fire spuriously on whatever happened during the
        // quiet window.
        s.specialsPrev = CountPartySpecials();
        return;
    }

    int specialsNow = CountPartySpecials();

    // Edge: had specials, now have none — fire immediately (0 ms delay).
    if (s.specialsPrev > 0 && specialsNow == 0) {
        FireCue("edge", specialsNow, now);
        s.specialsPrev = specialsNow;
        return;
    }

    // Repeat heartbeat: continuously empty, last tick aged out.
    if (specialsNow == 0) {
        // First time we observe empty post-gate also goes through the
        // repeat path so the player gets at least one signal even if
        // combat began with everyone already on auto.
        DWORD elapsedSinceLast = (s.lastTickAt == 0)
            ? (now - s.combatEnteredAt)
            : (now - s.lastTickAt);
        if (elapsedSinceLast >= kRepeatPeriodMs) {
            FireCue(s.lastTickAt == 0 ? "first-empty" : "repeat",
                    specialsNow, now);
        }
    }

    s.specialsPrev = specialsNow;
}

}  // namespace acc::combat::special_watch
