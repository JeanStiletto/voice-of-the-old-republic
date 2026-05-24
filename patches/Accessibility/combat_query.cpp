#include "combat_query.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetObjectName, GetObjectKind, ResolveServerObjectHandle, GetObjectPosition
#include "engine_manager.h"   // kAddrGuiManagerPtr
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKind / IdentifyPanel
#include "engine_player.h"    // GetPlayerServerCreature, GetActiveLeaderName, GetPlayerPosition
#include "engine_reads.h"
#include "examine_view.h"     // EffectName — shared EFFECT_TYPES → display
#include "hotkeys.h"
#include "log.h"
#include "menus_extract.h"    // FromControl — for Examine message-box read
#include "strings.h"
#include "prism.h"

namespace acc::combat::query {

namespace {

// ============================================================================
// Stat-snapshot reader — pulls everything needed for Phase 2A from one
// CSWSCreature*. Same data path Phase 2B reads.
// ============================================================================

struct StatSnap {
    int  hpCur, hpMax;
    int  fpMax;          // current FP — no clean engine accessor; stays 0
    int  ac;
    int  attrs[6];       // STR DEX CON INT WIS CHA
    int  fortSave, refSave, willSave;
    int  alignment;      // 0..100 (0 = dark, 100 = light)
    int  effectsCount;
    bool dead;
};

typedef int (__thiscall* PFN_GetIntThiscall)(void* this_);
typedef int (__thiscall* PFN_GetIntStatsThiscall)(void* this_);
typedef int (__thiscall* PFN_GetIntThisInt)(void* this_, int arg);

// Read the CSWSCreatureStats* via the +0xa74 offset.
void* ReadCreatureStats(void* serverCreature) {
    if (!serverCreature) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureStatsPtrOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Call a __thiscall accessor that returns int, defensively.
int CallIntAccessor(void* this_, uintptr_t addr) {
    if (!this_) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetIntThiscall>(addr);
        return fn(this_);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Call a __thiscall(this, int) accessor. Required for engine getters
// that take a 1-int param even when our existing call sites never
// vary the argument: GetCurrentHitPoints / GetMaxHitPoints both have
// this signature (Lane SARIF), and using PFN_GetIntThiscall reads
// random caller-frame bytes as `param_1` — sometimes hitting the
// trivial path (`MOV AX, [ECX+0xdc]; RET 4`) and sometimes the
// sum-of-two-fields path (`MOV EAX, [ECX+0xe4]; ADD EAX, [ECX+0xdc];
// RET 4`). The wrong-path returns leaked into the bare-H readout as
// nonsense numbers that don't track damage.
int CallIntAccessorArg(void* this_, uintptr_t addr, int arg) {
    if (!this_) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetIntThisInt>(addr);
        return fn(this_, arg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

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

// Read CSWSCreatureStats.feats CExoArrayList<ushort> size at +0x4. Direct
// field read — no engine call, safe for any path. Returns -1 on fault.
int ReadFeatCount(void* stats) {
    if (!stats) return -1;
    __try {
        auto* lst = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(stats) +
            kStatsFeatsListOffset);
        int s = lst->size;
        if (s < 0 || s > 0x400) return -1;  // sanity clamp
        return s;
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

// Read 6 attribute totals as bytes from creature_stats +0x34..+0x39. The
// plan's "Client-side" fields table documents this offset on
// CSWCCreatureStats but the byte layout matches CSWSCreatureStats per
// the swkotor.exe.h struct definitions. Both stats classes carry the same
// post-modifier totals.
void ReadAttrTotals(void* stats, int outAttrs[6]) {
    for (int i = 0; i < 6; ++i) outAttrs[i] = 0;
    if (!stats) return;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(stats);
        for (int i = 0; i < 6; ++i) {
            outAttrs[i] = static_cast<int>(
                *(base + kStatsAttrTotalsOffset + i));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        for (int i = 0; i < 6; ++i) outAttrs[i] = 0;
    }
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

// Walk CSWSObject.effects and produce a comma-joined string of unique
// localized effect-type names — sighted-player parity for the buff/debuff
// icon row on the target portrait. Mapped types only (unmapped engine
// types skipped, since they'd surface as "Effect #N" noise during auto-
// firing Q/E cycle announcements; user can Shift+H for full enumeration).
// Caps at 5 distinct names to keep speech terse.
//
// Returns true when at least one named effect was written; outBuf is
// empty-string on false.
bool BuildEffectsSummary(void* serverObject, char* outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 2) return false;
    outBuf[0] = '\0';
    if (!serverObject) return false;

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
            if (!name) continue;  // unmapped — skip in brief, Shift+H lists it

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
        int v = fn(serverObject);
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

bool ReadStatSnap(void* serverCreature, StatSnap& out) {
    std::memset(&out, 0, sizeof(out));
    if (!serverCreature) return false;
    void* stats = ReadCreatureStats(serverCreature);
    if (!stats) return false;

    // HP via CLIENT-side CSWCCreatureStats — see ReadCurrentHpFromClient
    // / ReadMaxHpFromClient for why we don't go through the server-side
    // engine getter (gates on stats.is_pc, returns garbage for non-PC).
    void* clientLeader = acc::engine::GetClientLeader();
    int curRead   = ReadCurrentHpFromClient(clientLeader);
    int maxRead   = ReadMaxHpFromClient(clientLeader);
    out.hpCur     = curRead < 0 ? 0 : curRead;
    out.hpMax     = maxRead < 0 ? 0 : maxRead;
    out.fpMax    = CallIntAccessor(serverCreature,
                                   kAddrCSWSCreatureGetMaxForcePoints);
    out.ac       = CallIntAccessor(serverCreature,
                                   kAddrCSWSCreatureGetArmorClass);
    out.fortSave = CallIntAccessor(stats, kAddrStatsGetFortSave);
    out.refSave  = CallIntAccessor(stats, kAddrStatsGetReflexSave);
    out.willSave = CallIntAccessor(stats, kAddrStatsGetWillSave);
    out.alignment = CallIntAccessor(stats,
                                    kAddrStatsGetSimpleAlignmentGoodEvil);
    ReadAttrTotals(stats, out.attrs);
    out.effectsCount = ReadEffectCount(serverCreature);
    int dead = CallIntAccessor(serverCreature, kAddrCSWSCreatureGetDead);
    out.dead = (dead != 0);
    return true;
}

}  // namespace

// ============================================================================
// Phase 2A — selected-PC full stat block.
// ============================================================================

bool SpeakSelectedPcStatBlock() {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        const char* phrase = acc::strings::Get(
            acc::strings::Id::PcStatNoCharacter);
        prism::Speak(phrase, /*interrupt=*/true);
        acclog::Write("Combat.PcStat", "no creature -> [%s]", phrase);
        return false;
    }
    StatSnap snap;
    if (!ReadStatSnap(creature, snap)) {
        const char* phrase = acc::strings::Get(
            acc::strings::Id::PcStatNoCharacter);
        prism::Speak(phrase, /*interrupt=*/true);
        acclog::Write("Combat.PcStat", "ReadStatSnap failed -> [%s]", phrase);
        return false;
    }

    char leader[64] = "";
    acc::engine::GetActiveLeaderName(leader, sizeof(leader));

    using S = acc::strings::Id;
    char msg[1024];
    size_t off = 0;
    auto append = [&](const char* fmt, auto... args) {
        if (off >= sizeof(msg)) return;
        int n = std::snprintf(msg + off, sizeof(msg) - off, fmt, args...);
        if (n > 0) off += static_cast<size_t>(n);
        if (off > sizeof(msg)) off = sizeof(msg);
    };

    if (leader[0]) {
        append("%s. ", leader);
    } else {
        append("%s ", acc::strings::Get(S::PcStatHeader));
    }
    if (snap.hpMax > 0 || snap.fpMax > 0) {
        append(acc::strings::Get(S::FmtPcStatHpFp),
               snap.hpCur, snap.hpMax, /*fpCur*/ snap.fpMax, snap.fpMax);
        append(" ");
    }
    append(acc::strings::Get(S::FmtPcStatAc), snap.ac);
    append(" ");
    append(acc::strings::Get(S::FmtPcStatAttrs),
           snap.attrs[0], snap.attrs[1], snap.attrs[2],
           snap.attrs[3], snap.attrs[4], snap.attrs[5]);
    append(" ");
    append(acc::strings::Get(S::FmtPcStatSaves),
           snap.fortSave, snap.refSave, snap.willSave);
    append(" ");
    append(acc::strings::Get(S::FmtPcStatAlignment), snap.alignment);
    if (snap.effectsCount > 0) {
        append(" ");
        append(acc::strings::Get(S::FmtPcStatEffectsHeader),
               snap.effectsCount);
    }

    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.PcStat",
                  "spoke leader=[%s] hp=%d/%d fp=%d ac=%d "
                  "attrs=%d/%d/%d/%d/%d/%d saves=%d/%d/%d align=%d eff=%d",
                  leader, snap.hpCur, snap.hpMax, snap.fpMax, snap.ac,
                  snap.attrs[0], snap.attrs[1], snap.attrs[2],
                  snap.attrs[3], snap.attrs[4], snap.attrs[5],
                  snap.fortSave, snap.refSave, snap.willSave,
                  snap.alignment, snap.effectsCount);
    return true;
}

void TickLeaderChangeAutoAnnounce() {
    // Skeleton: only log + speak the leader name on change. The full
    // SpeakSelectedPcStatBlock is gated to user-initiated Shift+S only,
    // because the stat-read path calls suspected engine accessors
    // (GetMaxHitPoints / GetArmorClass / save accessors) that haven't
    // been live-validated. A wrong address corrupts the stack canary
    // and __fastfails uncatchably (cause of 2026-05-09 crash).
    //
    // Once the accessor addresses are validated against a live binary,
    // restore the auto-fire path by replacing the leader-name speak
    // below with `SpeakSelectedPcStatBlock();`.

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

    static char s_lastLeader[64] = "";
    char now[64] = "";
    if (!acc::engine::GetActiveLeaderName(now, sizeof(now))) return;
    if (now[0] == '\0') return;
    if (std::strcmp(now, s_lastLeader) == 0) return;

    bool wasFirstObservation = (s_lastLeader[0] == '\0');
    std::strncpy(s_lastLeader, now, sizeof(s_lastLeader) - 1);
    s_lastLeader[sizeof(s_lastLeader) - 1] = '\0';

    if (wasFirstObservation) {
        acclog::Write("Combat.PcStat", "first leader observed=[%s]; suppress",
                      now);
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
// Phase 2C — Shift+H Examine hotkey.
// ============================================================================

namespace {

// Reuse the resolution chain interact_hotkey uses. We don't link directly
// to it (cyclic header risk); local copy of the LastTarget read is small
// and already lives in passive_narrate / interact_hotkey.
typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_);
constexpr uintptr_t kAddrCClientExoAppGetLastTargetLocal = 0x005EDD80;

void* GetClientExoApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetClientExoAppInternal() {
    void* clientApp = GetClientExoApp();
    if (!clientApp) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

uint32_t ReadLastTargetHandle() {
    void* exoApp = GetClientExoApp();
    if (!exoApp) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetLastTarget>(
            kAddrCClientExoAppGetLastTargetLocal);
        return fn(exoApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

}  // namespace

void HotkeyShiftH() {
    uint32_t handle = ReadLastTargetHandle();
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::ExamineNoTarget);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Combat.Examine", "Shift+H -> no target [%s]", msg);
        return;
    }

    // KOTOR 1 has no rich engine-side creature-examine panel —
    // ShowExamineBox is a generic TLK-message-box opener (verified
    // 2026-05-22 from decomp of CSWGuiMessageBox::SetMessage). So
    // Shift+H stays as a structured speech readout that mirrors what a
    // sighted player would see on the radial-menu "Examine" overlay.
    char name[96] = "";
    bool gotName = acc::engine::GetObjectDisplayNameByHandle(
        handle, name, sizeof(name));
    void* obj = acc::engine::ResolveClientObjectHandle(handle);
    if (!obj) obj = acc::engine::ResolveServerObjectHandle(handle);
    if (!gotName || name[0] == '\0') {
        if (obj) acc::engine::GetObjectName(obj, name, sizeof(name));
    }
    if (name[0] == '\0') {
        const char* msg = acc::strings::Get(
            acc::strings::Id::ExamineFailed);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Combat.Examine",
                      "Shift+H -> handle 0x%08x failed name resolution [%s]",
                      handle, msg);
        return;
    }

    int kind = obj ? acc::engine::GetObjectKind(obj) : -1;
    char msg[640];
    int effCount = -1;
    int featCount = -1;
    if (obj && kind == static_cast<int>(acc::engine::GameObjectKind::Creature)) {
        // Build the same brief Q/E speaks (name + faction + hp + distance
        // + main-hand weapon), then append inspect-only fields the
        // sighted player wouldn't see from a target reticle but expects
        // from an Examine action.
        BuildTargetCombatBrief(obj, name, msg, sizeof(msg));

        BriefBuf b{msg, sizeof(msg), std::strlen(msg)};
        effCount = ReadEffectCount(obj);
        if (effCount > 0) {
            BriefAppend(b,
                acc::strings::Get(acc::strings::Id::FmtBriefEffectsCount),
                effCount);
        }
        void* stats = ReadCreatureStats(obj);
        featCount = ReadFeatCount(stats);
        if (featCount > 0) {
            BriefAppend(b,
                acc::strings::Get(acc::strings::Id::FmtBriefFeatsCount),
                featCount);
        }
    } else {
        std::snprintf(msg, sizeof(msg), "%s.", name);
    }
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.Examine",
                  "Shift+H handle=0x%08x name=[%s] kind=%d effects=%d "
                  "feats=%d -> [%s]",
                  handle, name, kind, effCount, featCount, msg);
}

void TickExaminePanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return;

    void* examinePanel = nullptr;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        if (acc::engine::IdentifyPanel(p) ==
            acc::engine::PanelKind::Examine) {
            examinePanel = p;
            break;
        }
    }

    static void* s_lastPanel = nullptr;
    if (!examinePanel) {
        if (s_lastPanel) {
            acclog::Write("Combat.Examine", "panel closed");
            s_lastPanel = nullptr;
        }
        return;
    }
    if (examinePanel == s_lastPanel) return;
    s_lastPanel = examinePanel;

    // Panel just opened. Speech is owned by two other paths now:
    //   1. HotkeyShiftH speaks the brief opener at press time (name +
    //      faction + hp + distance + weapon).
    //   2. menus_listbox::kExamineSpec speaks each row on Up/Down nav.
    // TickExaminePanel just logs the open/close edges so we can see the
    // panel lifecycle in patch.log without speaking anything redundant.
    //
    // Row count is logged once for diagnostics — useful to confirm the
    // engine populated the listbox (rowCount > 0) vs left it empty
    // (which would mean vtable[27] didn't fire as expected).
    int rowCount = -1;
    __try {
        void* lb = reinterpret_cast<unsigned char*>(examinePanel) +
                   kExaminePanelListBoxOffset;
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) +
            kListBoxControlsOffset);
        rowCount = (lbList && lbList->data) ? lbList->size : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rowCount = -1;
    }
    acclog::Write("Combat.Examine",
                  "panel opened panel=%p rows=%d (speech: brief at press, "
                  "rows via menus_listbox kExamineSpec)",
                  examinePanel, rowCount);
}

// ----------------------------------------------------------------------------
// Win32 polling for Shift+H. Same pattern as interact_hotkey's PollHotkey.
// ----------------------------------------------------------------------------

void PollWin32Hotkey() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::ExamineOpen)) return;

    // Self-gate on player-loaded — Shift+H is only meaningful in-world.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    HotkeyShiftH();
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
                  "hp=%d/%d effects=[%s] main=[%s] off=[%s] -> [%s]",
                  hpCur, hpMax,
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
