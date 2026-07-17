#include "combat_query.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetObjectKind, GetObjectPosition, GetObjectDisplayNameByHandle
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKindName — UI-block log
#include "engine_player.h"    // GetPlayerServerCreature, GetActiveLeaderName, GetPlayerPosition
#include "engine_reads.h"
#include "examine_view.h"     // EffectName — shared EFFECT_TYPES → display
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "prism.h"
#include "transitions.h"      // IsModuleLoadPending — gate during cutscene-load
                              // transient (engine LYT loader use-after-free)

namespace acc::combat::query {

namespace {

typedef int (__thiscall* PFN_GetIntThiscall)(void* this_);

// HP read helpers — direct CSWCCreatureStats reads, the same struct
// the engine's character-sheet panel renders from. Path:
//
//   CSWCCreature  (client leader)
//     +0x2f8  -> CSWCLevelUpStats* (embeds CSWCCreatureStats at +0)
//                +0x48   short  hit_points          (BASE HP — ignore)
//                +0x4c   short  pregame_current_hp  (LIVE current HP)
//                +0x4e   short  max_hit_points      (LIVE max HP)
//
// Verified live 2026-05-24 via the per-field HpProbe: cstats.4c
// tracks damage/heals across PC + companions; cstats.4e is the same
// number the character sheet displays as "X / Y" max (48 for the PC,
// 75 for Bastila, 66 for Carth — all matching).
//
// The corresponding short on the SERVER object at CSWSObject+0xdc
// happens to also equal pregame_current_hp / cstats.4c (engine
// mirrors the value across server/client every tick), so we keep
// the server-side path as a fallback when the client chain doesn't
// resolve.
//
// The previous engine-accessor path (CSWSCreature::GetMaxHitPoints @
// 0x004ed310 with param_1=1) gates internally on stats[+0x6c] (is_pc)
// and returns garbage for any non-PC creature, which is why Tab to
// Carth used to read "60" (random obj.e0 base) instead of "50/66".
constexpr size_t kClientCreatureLvlUpStatsOffset = 0x2f8;
constexpr size_t kClientStatsCurrentHpOffset     = 0x4c;  // pregame_current_hp
constexpr size_t kClientStatsMaxHpOffset         = 0x4e;  // max_hit_points

int ReadCurrentHpFromClient(void* clientLeader) {
    if (!clientLeader) return -1;
    void* lvlUpStats = nullptr;
    __try {
        lvlUpStats = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientLeader) +
            kClientCreatureLvlUpStatsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    if (!lvlUpStats) return -1;
    __try {
        return static_cast<int>(*reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(lvlUpStats) +
            kClientStatsCurrentHpOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int ReadMaxHpFromClient(void* clientLeader) {
    if (!clientLeader) return -1;
    void* lvlUpStats = nullptr;
    __try {
        lvlUpStats = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientLeader) +
            kClientCreatureLvlUpStatsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    if (!lvlUpStats) return -1;
    __try {
        return static_cast<int>(*reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(lvlUpStats) +
            kClientStatsMaxHpOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Walk CSWSCreature.inventory @+0xa2c → CSWInventory.<slot>_handle →
// CClientExoApp::GetObjectName(handle). SEH-guarded at every hop.
// `slotHandleOffset` is one of kInventory*HandleOffset (e.g.
// kInventoryRightWeaponHandleOffset for main-hand). Returns true and
// writes a null-terminated display name on success; false on any
// null link, empty slot, sentinel handle, or name-resolution miss.
// Logs the slot handle so an unresolved item leaves a breadcrumb.
bool ReadEquippedItemName(void* serverCreature, size_t slotHandleOffset,
                          const char* slotLabel,
                          char* outBuf, size_t outBufSize) {
    if (!serverCreature || !outBuf || outBufSize < 2) return false;
    outBuf[0] = '\0';

    void* inventory = nullptr;
    __try {
        inventory = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureInventoryOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!inventory) return false;

    uint32_t handle = 0;
    __try {
        handle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(inventory) + slotHandleOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        return false;  // empty slot
    }

    bool ok = acc::engine::GetObjectDisplayNameByHandle(
        handle, outBuf, outBufSize);
    if (!ok || outBuf[0] == '\0') {
        acclog::Write("Combat.Equip",
                      "slot=%s handle=0x%08x name-resolve missed",
                      slotLabel ? slotLabel : "?", handle);
        return false;
    }
    return true;
}

// 2D distance (horizontal plane) from player to target in metres. Matches
// the cycle_state convention — z deliberately ignored so multi-storey
// targets don't get misleading larger numbers. Returns -1.0f if either
// position read fails.
float ComputePlayerDistanceMeters(void* targetObject) {
    if (!targetObject) return -1.0f;
    Vector tgt{};
    if (!acc::engine::GetObjectPosition(targetObject, tgt)) return -1.0f;
    Vector pc{};
    if (!acc::engine::GetPlayerPosition(pc)) return -1.0f;
    float dx = tgt.x - pc.x;
    float dy = tgt.y - pc.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Appendable composer — writes `fmt`-formatted args into `buf` at the
// current offset and advances. No-op if `buf` already saturated; never
// overflows. Used to chain optional suffix clauses onto the brief.
struct BriefBuf {
    char*  buf;
    size_t cap;
    size_t off;
};

void BriefAppend(BriefBuf& b, const char* fmt, ...) {
    if (!b.buf || b.off >= b.cap) return;
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(b.buf + b.off, b.cap - b.off, fmt, args);
    va_end(args);
    if (n > 0) b.off += static_cast<size_t>(n);
    if (b.off > b.cap) b.off = b.cap;
}

// Read the runtime-effects array length on CSWSObject.effects @+0x124
// (CExoArrayList<CSWSEffect*>). Cheap and tells us "is this creature
// affected right now". We don't enumerate the effects themselves in the
// skeleton — the per-effect name resolution path is an "open" item per
// docs/combat-system.md Pillar 2 §"Open".
int ReadEffectCount(void* serverObject) {
    if (!serverObject) return 0;
    __try {
        auto* lst = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(serverObject) +
            kObjectEffectsOffset);
        if (!lst) return 0;
        int s = lst->size;
        if (s < 0 || s > 0x4000) return 0;
        return s;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Read the creature's effect-icon row (CSWSCreature.effect_icons — the
// exact icon list the sighted portrait renders; see engine_offsets.h) and
// join the localized icon names. Returns the number of names written, 0
// when the row is empty / unreadable / the object is not a creature.
// The engine keeps the array priority-sorted and deduped by icon id, so
// array order is the sighted display order.
int BuildEffectIconSummary(void* serverObject, char* outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 2) return 0;
    outBuf[0] = '\0';
    if (!serverObject) return 0;
    if (acc::engine::GetObjectKind(serverObject) !=
        static_cast<int>(acc::engine::GameObjectKind::Creature)) {
        return 0;
    }

    constexpr int kMaxSpoken = 5;
    int written = 0;
    __try {
        void** data = *reinterpret_cast<void***>(
            reinterpret_cast<unsigned char*>(serverObject) +
            kCreatureEffectIconsDataOffset);
        int size = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(serverObject) +
            kCreatureEffectIconsSizeOffset);
        if (!data || size <= 0 || size > 64) return 0;

        size_t off = 0;
        for (int i = 0; i < size && written < kMaxSpoken; ++i) {
            void* icon = data[i];
            if (!icon) continue;
            int id = static_cast<int>(*reinterpret_cast<unsigned short*>(
                reinterpret_cast<unsigned char*>(icon) +
                kEffectIconObjectIdOffset));
            const char* name = acc::examine_view::EffectIconName(id);
            if (!name) continue;  // NULL_ICON / alignment gauges
            int n = std::snprintf(outBuf + off, outBufSize - off,
                                  (off == 0) ? "%s" : ", %s", name);
            if (n < 0) break;
            off += static_cast<size_t>(n);
            ++written;
            if (off >= outBufSize) { off = outBufSize - 1; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Partial result is fine.
    }
    return outBuf[0] ? written : 0;
}

// Produce a comma-joined effects string for the brief / self status.
// Primary source: the effect-icon row (sighted parity — the same icons a
// sighted player sees on the portrait, by name, in display order). When a
// creature has effects but no icons (script-applied buffs without an
// EFFECTICON — e.g. Malak's fight buffs), fall back to the legacy walk of
// CSWSObject.effects mapping raw EFFECT_TYPES, so no information that was
// spoken before this rework is lost. Mapped names only, capped at 5.
//
// Returns true when at least one name was written; outBuf is empty-string
// on false.
bool BuildEffectsSummary(void* serverObject, char* outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 2) return false;
    outBuf[0] = '\0';
    if (!serverObject) return false;

    if (BuildEffectIconSummary(serverObject, outBuf, outBufSize) > 0) {
        return true;
    }

    constexpr int kMaxDistinct = 5;
    int seenTypes[kMaxDistinct] = {0};
    int seenCount = 0;

    __try {
        auto* lst = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(serverObject) +
            kObjectEffectsOffset);
        if (!lst || !lst->data || lst->size <= 0) return false;
        int n = lst->size > 64 ? 64 : lst->size;
        size_t off = 0;
        for (int i = 0; i < n && seenCount < kMaxDistinct; ++i) {
            void* eff = lst->data[i];
            if (!eff) continue;
            int type = static_cast<int>(*reinterpret_cast<unsigned short*>(
                reinterpret_cast<unsigned char*>(eff) +
                kGameEffectTypeOffset));
            const char* name = acc::examine_view::EffectName(type);
            if (!name) continue;  // unmapped — skip in brief, Ö examine lists it

            bool dup = false;
            for (int j = 0; j < seenCount; ++j) {
                if (seenTypes[j] == type) { dup = true; break; }
            }
            if (dup) continue;
            seenTypes[seenCount++] = type;

            int written = std::snprintf(
                outBuf + off, outBufSize - off,
                (off == 0) ? "%s" : ", %s", name);
            if (written < 0) break;
            off += static_cast<size_t>(written);
            if (off >= outBufSize) { off = outBufSize - 1; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Partial result is fine; whatever we already wrote is valid.
    }
    return outBuf[0] != '\0';
}

// CSWSObject::GetDamageLevel @0x4cb020 — `ulong __thiscall(this)`.
// Returns the 0..5 wound-state bucket (decompile-verified thresholds:
// >=95 healthy, >=75 light, >=50 wounded, >=25 badly, >0 dying, <=0 dead).
// Validated 2026-05-22 live; safe for auto-firing brief path.
int ReadDamageLevelDirect(void* serverObject) {
    if (!serverObject) return -1;
    __try {
        auto fn = reinterpret_cast<PFN_GetIntThiscall>(
            kAddrCSWSObjectGetDamageLevel);
        // GetDamageLevel returns ulong, but the 0..5 bucket is only the low
        // byte (AL). For buckets 0..3 the engine leaves comparison-flag
        // garbage in the upper 3 bytes (decompile @0x4cb020), so reading the
        // full 32-bit value blows past the range check and clamps to -1 —
        // only the clean dying/dead returns (4/5) survived. Mask to AL.
        int v = fn(serverObject) & 0xFF;
        if (v < 0 || v > 5) return -1;
        return v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

acc::strings::Id DamageLevelStringIdFor(int level) {
    using S = acc::strings::Id;
    switch (level) {
        case 0:  return S::DamageLevel0Healthy;
        case 1:  return S::DamageLevel1Light;
        case 2:  return S::DamageLevel2Wounded;
        case 3:  return S::DamageLevel3Badly;
        case 4:  return S::DamageLevel4Dying;
        case 5:  return S::DamageLevel5Dead;
        default: return S::DamageLevel0Healthy;
    }
}

}  // namespace

void TickLeaderChangeAutoAnnounce() {
    // Player-loaded gate — don't probe CClientExoApp::GetPlayerCharacterName
    // until the world has actually loaded. During the chargen→world
    // transient, the player slot is half-initialised and hammering the
    // engine accessor every frame intermittently wedges the load (the
    // world never spins up, engine alive but no module ever loads).
    // Reproduced 2026-05-19 on chargen→world: Combat.PcStat fires once
    // with "temp" at frame ~667, then 1300+ frames tick with no further
    // engine state change until the user gives up and quits.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    // Module-load latch — covers the cutscene-load transient that
    // GetPlayerPosition can't catch (old module still alive, gate keeps
    // returning true straight through the handoff). PartySelection-OK →
    // stunt_03a crashed deep in CLYT::LoadLayout on a decommitted
    // resref page in dump swkotor.exe(1).23224.dmp; the spammy
    // `PartyLeader: leader=handle —` log from this probe was the only
    // per-tick activity firing into the engine's teardown window.
    if (acc::transitions::IsModuleLoadPending()) return;

    static char  s_lastLeader[64]   = "";
    static void* s_lastArea         = nullptr;
    static DWORD s_areaChangeTick    = 0;

    // Track area-pointer changes. A save-game load OR a module transition
    // re-establishes the party, so the leader "change" that follows is the
    // engine wiring up the new party — not a user Tab-switch — and announcing
    // the PC's name talks over the area-name cue. We re-baseline silently for
    // a short grace window after any area change. The load_from_savegame flag
    // can't anchor this: it clears a hair before the new leader becomes
    // readable (patch-20260614-165512.log — "Bereich: …" and the leader
    // change both land after the flag has dropped), so the gate missed. The
    // area-pointer change is the robust anchor — it's exactly what
    // transitions::SpeakArea fires on.
    void* area = acc::engine::GetCurrentArea();
    if (area && area != s_lastArea) {
        s_lastArea       = area;
        s_areaChangeTick = GetTickCount();
    }

    char now[64] = "";
    if (!acc::engine::GetActiveLeaderName(now, sizeof(now))) return;
    if (now[0] == '\0') return;
    if (std::strcmp(now, s_lastLeader) == 0) return;

    bool wasFirstObservation = (s_lastLeader[0] == '\0');
    // ~3s covers the lag between the area pointer surfacing and the new
    // leader name becoming readable (observed simultaneous, same second).
    constexpr DWORD kAreaChangeGraceMs = 3000;
    bool recentAreaChange =
        s_areaChangeTick != 0 &&
        (GetTickCount() - s_areaChangeTick) < kAreaChangeGraceMs;
    std::strncpy(s_lastLeader, now, sizeof(s_lastLeader) - 1);
    s_lastLeader[sizeof(s_lastLeader) - 1] = '\0';

    if (wasFirstObservation) {
        acclog::Write("Combat.PcStat", "first leader observed=[%s]; suppress",
                      now);
        return;
    }
    if (recentAreaChange) {
        acclog::Write("Combat.PcStat",
                      "leader changed -> [%s] %lums after area change; suppress "
                      "(baseline updated)", now,
                      (unsigned long)(GetTickCount() - s_areaChangeTick));
        return;
    }
    acclog::Write("Combat.PcStat", "leader changed -> [%s]; speaking name only",
                  now);
    prism::Speak(now, /*interrupt=*/true);
}

// ============================================================================
// Phase 2B — opponent cycle-announcement enrichment.
// ============================================================================

bool BuildTargetCombatBrief(void* targetServerObject,
                            const char* targetName,
                            char* outBuf, size_t outBufSize)
{
    if (!targetServerObject || !outBuf || outBufSize < 4) return false;
    outBuf[0] = '\0';

    // Only enrich for Creature kind. Doors / items / waypoints have their
    // own enrichment paths in engine_area::GetObjectName.
    int kind = acc::engine::GetObjectKind(targetServerObject);
    if (kind != static_cast<int>(acc::engine::GameObjectKind::Creature)) {
        return false;
    }

    using S = acc::strings::Id;
    BriefBuf b{outBuf, outBufSize, 0};

    // 1. Name — always present.
    BriefAppend(b, acc::strings::Get(S::FmtTargetCombatBrief),
                targetName ? targetName : "?");

    // 2. Condition (damage-level bucket) — mirrors what the sighted
    //    player reads from the HP-bar colour. GetDamageLevel @0x4cb020
    //    returns 0..5 across exact hp/maxhp ratio thresholds
    //    (95/75/50/25/0%); skip when healthy (0) so common in-combat
    //    transitions stay silent.
    int dl = ReadDamageLevelDirect(targetServerObject);
    if (dl > 0) {
        BriefAppend(b, acc::strings::Get(S::FmtBriefCondition),
                    acc::strings::Get(DamageLevelStringIdFor(dl)));
    }

    // 3. Distance — sighted players can judge approximate range from the
    //    target reticle; expose the same affordance as a metres readout.
    //    2D horizontal distance (matches the cycle audio cue's range model).
    float distM = ComputePlayerDistanceMeters(targetServerObject);
    int distMeters = -1;
    if (distM >= 0.0f) {
        distMeters = static_cast<int>(distM + 0.5f);   // round to nearest
        BriefAppend(b, acc::strings::Get(S::FmtBriefDistanceMeters),
                    distMeters);
    }

    // 4. Status effects — sighted parity for the buff/debuff icon row on
    //    the target portrait. Dedup'd, mapped types only, capped at 5.
    char effects[192] = "";
    bool gotEffects = BuildEffectsSummary(targetServerObject,
                                          effects, sizeof(effects));
    if (gotEffects) {
        BriefAppend(b, acc::strings::Get(S::FmtBriefEffects), effects);
    }

    // 5. Main-hand weapon — what the sighted player sees the enemy
    //    holding. Skip silently when the slot is empty (unarmed
    //    creatures, animals, etc.) rather than announcing "unarmed" —
    //    verbose for the common case where the user already knows what
    //    an animal looks like.
    char mainWpn[96] = "";
    bool gotMain = ReadEquippedItemName(targetServerObject,
                                        kInventoryRightWeaponHandleOffset,
                                        "main-hand",
                                        mainWpn, sizeof(mainWpn));
    if (gotMain && mainWpn[0] != '\0') {
        BriefAppend(b, acc::strings::Get(S::FmtBriefWielding), mainWpn);
    }

    // 6. Off-hand weapon — dual-wield / shield / off-hand pistol parity.
    //    Sighted player sees both icons on the enemy's portrait.
    char offWpn[96] = "";
    bool gotOff = ReadEquippedItemName(targetServerObject,
                                       kInventoryLeftWeaponHandleOffset,
                                       "off-hand",
                                       offWpn, sizeof(offWpn));
    if (gotOff && offWpn[0] != '\0') {
        BriefAppend(b, acc::strings::Get(S::FmtBriefOffHand), offWpn);
    }

    acclog::Write("Combat.Brief",
                  "name=[%s] dl=%d distM=%.2f effects=[%s] main=[%s] "
                  "off=[%s]",
                  targetName ? targetName : "?", dl, distM,
                  gotEffects ? effects : "",
                  gotMain ? mainWpn : "",
                  gotOff ? offWpn : "");
    return true;
}

// ============================================================================
// Bare-H self status — leader HP / effects / equipped weapon.
// ============================================================================

void SpeakSelfStatus() {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        const char* phrase = acc::strings::Get(
            acc::strings::Id::PcStatNoCharacter);
        prism::Speak(phrase, /*interrupt=*/true);
        acclog::Write("Combat.SelfStatus", "no creature -> [%s]", phrase);
        return;
    }

    void* clientLeader = acc::engine::GetClientLeader();
    int hpCurRaw = ReadCurrentHpFromClient(clientLeader);
    int hpMaxRaw = ReadMaxHpFromClient(clientLeader);
    int hpCur = hpCurRaw < 0 ? 0 : hpCurRaw;
    int hpMax = hpMaxRaw < 0 ? 0 : hpMaxRaw;

    using S = acc::strings::Id;
    BriefBuf b{nullptr, 0, 0};
    char msg[384];
    b.buf = msg;
    b.cap = sizeof(msg);

    // Prefer "%d of %d" when the engine resolved a sane max. Some
    // creature shapes (driving / minigame slots) leave the max at 0;
    // fall back to the cur-only phrase rather than speak "X of 0".
    if (hpMax > 0 && hpCur <= hpMax + 32) {
        BriefAppend(b, acc::strings::Get(S::FmtSelfStatusHpOf),
                    hpCur, hpMax);
    } else {
        BriefAppend(b, acc::strings::Get(S::FmtSelfStatusHp), hpCur);
    }

    // Force pool — only for Force users. max_force_points is 0 for non-Jedi
    // classes and droids, so a positive max is the "has the Force" signal.
    int fpCur = 0, fpMax = 0;
    if (acc::engine::ReadCreatureForcePoints(clientLeader, &fpCur, &fpMax) &&
        fpMax > 0) {
        if (fpCur < 0) fpCur = 0;
        BriefAppend(b, acc::strings::Get(S::FmtSelfStatusFpOf), fpCur, fpMax);
    }

    char effects[192] = "";
    bool gotEffects = BuildEffectsSummary(creature, effects, sizeof(effects));
    if (gotEffects) {
        BriefAppend(b, acc::strings::Get(S::FmtBriefEffects), effects);
    }

    char mainWpn[96] = "";
    bool gotMain = ReadEquippedItemName(creature,
                                        kInventoryRightWeaponHandleOffset,
                                        "main-hand",
                                        mainWpn, sizeof(mainWpn));
    if (gotMain && mainWpn[0] != '\0') {
        BriefAppend(b, acc::strings::Get(S::FmtBriefWielding), mainWpn);
    }

    char offWpn[96] = "";
    bool gotOff = ReadEquippedItemName(creature,
                                       kInventoryLeftWeaponHandleOffset,
                                       "off-hand",
                                       offWpn, sizeof(offWpn));
    if (gotOff && offWpn[0] != '\0') {
        BriefAppend(b, acc::strings::Get(S::FmtBriefOffHand), offWpn);
    }

    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.SelfStatus",
                  "hp=%d/%d fp=%d/%d effects=[%s] main=[%s] off=[%s] -> [%s]",
                  hpCur, hpMax, fpCur, fpMax,
                  gotEffects ? effects : "",
                  gotMain ? mainWpn : "",
                  gotOff  ? offWpn  : "",
                  msg);
}

void PollWin32SelfStatusHotkey() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::SelfStatusAnnounce)) return;

    // Player-loaded gate. Reading HP / effects / inventory off a half-
    // initialised player slot during chargen→world transient is one of
    // the documented crash paths (project_chargen_world_transient_unsafe_probes).
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    // UI-block gate — H may have meaning inside a future menu binding;
    // for now, drop it when any blocking panel is foreground so the
    // readout never overlaps inventory / map / dialog speech. Same gate
    // Tab leader-announce uses.
    acc::engine::UiBlockState ui;
    if (acc::engine::IsForegroundUiBlocking(&ui)) {
        acclog::Write("Combat.SelfStatus",
                      "H suppressed — ui blocked (fg=%p kind=%s)",
                      ui.fgPanel,
                      ui.fgPanel ? acc::engine::PanelKindName(ui.fgKind)
                                 : "?");
        return;
    }

    SpeakSelfStatus();
}

}  // namespace acc::combat::query
