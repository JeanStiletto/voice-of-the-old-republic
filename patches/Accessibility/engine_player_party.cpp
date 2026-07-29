// party and leader resolution.
//
// Split out of engine_player.cpp by the Phase-1 structure pass
// (refactoring candidate 5). Everything here answers "who is in the party,
// who is leading, what are they called": leader/creature resolution, the
// party table accessors, solo mode, roster availability/selectability and
// the companion name table.
//
// The functions were interleaved with the input-lock state machine in the
// original file, so this is a two-piece cut rather than one slice - the
// definitions themselves are verbatim.
//
// Declarations stay in engine_player.h; GetPlayerServerObject comes from
// engine_player_internal.h.

#include "engine_player.h"
#include "engine_player_internal.h"

#include <windows.h>
#include <cstdint>

#include "engine_area.h"
#include "engine_reads.h"
#include "log.h"
#include "engine_rebase.h"

namespace acc::engine {

void* GetPlayerServerCreature() {
    return GetPlayerServerObject();
}

void* GetClientLeader() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return nullptr;
        auto fn = reinterpret_cast<PFN_GetPlayerCreature>(
            kAddrGetPlayerCreature);
        return fn(exoApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int SetLeaderQueueModeBit(int on) {
    // CSWCCreature.field200_0x440 — same offset combat_diag reads as `cm`,
    // set by CSWCCreature::SetCombatMode @0x00610a10. Bit 0 is the
    // replace-vs-append discriminator both action dispatchers branch on.
    constexpr size_t kCSWCCreatureCombatModeOffset = 0x440;
    void* leader = GetClientLeader();
    if (!leader) return -1;
    __try {
        uint8_t* p = reinterpret_cast<uint8_t*>(leader) +
                     kCSWCCreatureCombatModeOffset;
        uint8_t prev = *p;
        if (on) *p = static_cast<uint8_t>(prev | 0x01u);
        else    *p = static_cast<uint8_t>(prev & ~0x01u);
        return prev & 0x01;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool IsAnyPartyMemberInCombat() {
    // The global combat flag we poll (CClientExoApp::GetCombatMode) only ever
    // mirrors the *controlled* leader's per-creature combat bit — CSWParty::
    // SetLeader re-syncs it on every Tab (call-at 0x0063596d), so switching to
    // a not-yet-engaged member reads as "combat ended" mid-encounter. The bit
    // is in fact maintained for ALL party members every tick by
    // CClientExoAppInternal::UpdateCombatMode @0x005f3ad0, which loops
    // CSWParty::GetCharacter and calls CSWCCreature::SetCombatMode per member.
    // OR that bit across the whole party for the true encounter state. Stays
    // client-side on the same field/namespace as the global we already poll,
    // so there is no client/server timing skew between the two.
    constexpr size_t    kInternalPartyOffset          = 0x270;     // CClientExoAppInternal.party
    constexpr size_t    kCSWCCreatureCombatModeOffset = 0x440;     // field200_0x440 bit 0
    const uintptr_t kAddrCSWPartyGetCharacter = acc::addr::R(0x006346C0); // __thiscall(int) -> CSWCCreature*
    constexpr int       kPartyScanCap                 = 8;          // KOTOR party <=3; generous cap
    typedef void* (__thiscall* PFN_GetCharacter)(void* this_, int index);
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return false;
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
        if (!internal) return false;
        void* party = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) + kInternalPartyOffset);
        if (!party) return false;
        auto getChar = reinterpret_cast<PFN_GetCharacter>(
            kAddrCSWPartyGetCharacter);
        // GetCharacter is internally bounds-checked (returns null for any
        // index >= party_count), so a fixed cap with a null-skip is safe even
        // though party_member_datas is contiguous.
        for (int i = 0; i < kPartyScanCap; ++i) {
            void* c = getChar(party, i);
            if (!c) continue;
            uint8_t bits = *(reinterpret_cast<uint8_t*>(c) +
                             kCSWCCreatureCombatModeOffset);
            if (bits & 0x01u) return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetActiveLeaderName(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    // Resolve the *currently controlled* leader's name. Tab cycles which
    // party member the engine considers leader; GetClientLeader walks
    // CClientExoApp::GetPlayerCreature which the engine re-wires on each
    // Tab to the new leader (confirmed live via the DiagSelect Tab probe).
    //
    // Read first_name directly from CSWSCreatureStats via a pure memory
    // path (ExtractTextOrStrRef — inline CExoString, falling back to TLK
    // strref). We intentionally do NOT route through the engine's
    // CClientExoApp::GetObjectName accessor: that accessor writes through
    // a stack CExoString, and on the PC handle during the chargen→world
    // transient it overruns the caller's stack frame and trips the /GS
    // canary → uncatchable __fastfail (bisected 2026-05-19). The
    // direct-read path here doesn't invoke the engine accessor at all and
    // is safe in every state.
    //
    // The PC's stats.first_name is empty in vanilla saves (chargen writes
    // the chosen name to CClientExoAppInternal::player_character_name
    // instead — see project_pc_name_lives_in_client_exoapp). So when the
    // direct-read path yields an empty string, the leader is the PC and
    // we fall back to GetPlayerCharacterName.
    void* clientLeader = GetClientLeader();
    void* serverCreature = nullptr;
    void* stats = nullptr;
    uint32_t leaderHandle = 0;
    if (clientLeader) {
        __try {
            serverCreature = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(clientLeader) +
                kClientObjectServerObjectOffset);
            if (serverCreature) {
                stats = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(serverCreature) +
                    kCreatureStatsPtrOffset);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            serverCreature = nullptr;
            stats = nullptr;
        }
        leaderHandle = GetObjectHandle(clientLeader);
    }

    // Path 1: engine's universal display-name accessor on the leader's
    // handle. This is the same accessor sighted UI uses; it gives
    // localized names for companions (Trask, Carth, ...) and the PC's
    // chargen name where appropriate. The crash window we hit on
    // 2026-05-19 was the chargen→world *transient* (PC handle not yet
    // fully registered) — the Tab caller already gates that with
    // GetPlayerPosition.
    if (leaderHandle != 0u &&
        GetObjectDisplayNameByHandle(leaderHandle, outBuf, bufSize) &&
        outBuf[0] != '\0') {
        // Trace, not Write: this accessor is called dozens of times per
        // tick and the resolved line is identical every call (one distinct
        // value across a whole session). Trace dedups consecutive identical
        // content and emits a "(repeated Nx more)" summary on change, so the
        // log keeps the first line + every leader change + the repeat count
        // with none of the per-tick spam. (Was Write — 58% of one tester
        // session's 42k-line log; the volume served no diagnostic purpose
        // and each line cost an OutputDebugStringA + fflush on the game tick.)
        acclog::Trace("PartyLeader",
                      "leader=handle — client=%p server=%p stats=%p "
                      "handle=0x%08x name=[%s]",
                      clientLeader, serverCreature, stats,
                      leaderHandle, outBuf);
        return true;
    }

    // Path 1b: the controlled leader's *client* handle is the engine's
    // "player-controlled" sentinel (0xffffffff) whose display name resolves
    // empty — so Path 1 dies for the PC and we'd fall through to the stale
    // chargen slot (GetPlayerCharacterName returns a leftover "test" even
    // when the player chose a different, longer name). The PC's *server*
    // creature keeps its real object identity, so resolve the name straight
    // off it with the same by-pointer accessor the Q/E cycle uses — verified
    // live: cycling onto the PC from a companion's view reads the correct
    // name, because that path reads the server object too. Companions never
    // reach here (Path 1 already returned for them).
    outBuf[0] = '\0';
    if (serverCreature &&
        GetObjectName(serverCreature, outBuf, bufSize) &&
        outBuf[0] != '\0') {
        acclog::Trace("PartyLeader",
                      "leader=server-object — client=%p server=%p name=[%s]",
                      clientLeader, serverCreature, outBuf);
        return true;
    }

    // Path 2: direct stats first_name read. Bypasses the engine accessor
    // entirely (pure memory read). Companions usually populate this; the
    // PC's stats slot is empty in vanilla saves.
    outBuf[0] = '\0';
    bool statsReadOk = false;
    if (stats) {
        statsReadOk = ExtractTextOrStrRef(
            stats,
            kCreatureStatsFirstNameOffset,
            kCreatureStatsFirstNameOffset + 4,
            outBuf, bufSize);
        if (statsReadOk && outBuf[0] != '\0') {
            acclog::Trace("PartyLeader",
                          "leader=stats — client=%p server=%p stats=%p "
                          "name=[%s]",
                          clientLeader, serverCreature, stats, outBuf);
            return true;
        }
    }

    // Path 3: PC chargen-name slot in CClientExoAppInternal — last resort
    // when the leader is the PC and the engine accessor + stats are both
    // empty (the canonical case for a freshly created PC).
    outBuf[0] = '\0';
    bool pcOk = GetPlayerCharacterName(outBuf, bufSize);

    // Diagnostic: also dump the raw 8 bytes at stats+0x14 (CExoString
    // c_string + length) so we can see if first_name is a strref-only
    // slot we should be resolving through TLK.
    const char* firstName_cstr = nullptr;
    uint32_t    firstName_len  = 0xFFFFFFFFu;
    uint32_t    firstName_strref = 0xFFFFFFFFu;
    if (stats) {
        __try {
            auto* p = reinterpret_cast<unsigned char*>(stats) +
                      kCreatureStatsFirstNameOffset;
            firstName_cstr   = *reinterpret_cast<const char**>(p);
            firstName_len    = *reinterpret_cast<uint32_t*>(p + 4);
            firstName_strref = *reinterpret_cast<uint32_t*>(p + 8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    uint32_t serverHandle = serverCreature ? GetObjectHandle(serverCreature) : 0u;
    acclog::Trace("PartyLeader",
                  "all paths empty — client=%p server=%p serverHandle=0x%08x "
                  "stats=%p handle=0x%08x first_name(cstr=%p len=%u strref=0x%x) "
                  "stats_read_ok=%d pcOk=%d name=[%s]",
                  clientLeader, serverCreature, serverHandle, stats,
                  leaderHandle, firstName_cstr, firstName_len,
                  firstName_strref, statsReadOk ? 1 : 0, pcOk ? 1 : 0,
                  outBuf);
    if (pcOk && outBuf[0] != '\0') return true;
    return false;
}

bool GetPlayerCharacterName(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        void* exoApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
        if (!exoApp) return false;
        auto fn = reinterpret_cast<PFN_GetPlayerCharacterName>(
            kAddrCClientExoAppGetPlayerCharacterName);
        void* exoStr = fn(exoApp);
        if (!exoStr) return false;
        // CExoString layout (c_string @+0, length @+4) — same shape every
        // other reader hits via ReadCExoString.
        return ReadCExoString(exoStr, /*offset=*/0, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


int GetPartyMembers(uint32_t* outHandles, int maxCount) {
    if (!outHandles || maxCount <= 0) return 0;
    __try {
        // GetServerPartyTable walks AppManager+0x8 → facade → +0x4 internal
        // → +0x1b770. The old inline chain here skipped the +0x4 internal
        // indirection and read pt_num_members from facade heap, which lands
        // on a zero byte — so this always returned 0 ("no active party")
        // and every party check silently no-opped.
        auto* partyTable = reinterpret_cast<unsigned char*>(
            GetServerPartyTable());
        if (!partyTable) return 0;
        uint32_t numMembers = *reinterpret_cast<uint32_t*>(
            partyTable + kPartyTableNumMembersOffset);
        if (numMembers == 0 ||
            numMembers > static_cast<uint32_t>(kPartyTableMaxMembers)) {
            return 0;
        }
        int take = static_cast<int>(numMembers);
        if (take > maxCount) take = maxCount;
        // pt_member_ids holds NPC *roster slot indices* (0..8), NOT object
        // handles — e.g. 2=Carth, 6=Mission. Resolve each slot to the live
        // creature's object handle via CSWPartyTable::GetNPCObject so the
        // result is comparable to GetObjectHandle()/handle-keyed accessors.
        // Mirror OnPanelAdded's (slot,0,1) → (slot,1,1) second-instance
        // fallback. Slots that don't resolve to a live creature are skipped.
        typedef int (__thiscall* PFN_GetNPCObject)(void*, int, int, int);
        auto getObj = reinterpret_cast<PFN_GetNPCObject>(
            kAddrCSWPartyTableGetNPCObject);
        auto* ids = reinterpret_cast<int32_t*>(
            partyTable + kPartyTableMemberIdsOffset);
        int written = 0;
        for (int i = 0; i < take; ++i) {
            int slot = ids[i];
            int id = getObj(partyTable, slot, 0, 1);
            if (id == 0) id = getObj(partyTable, slot, 1, 1);
            if (id != 0) outHandles[written++] = static_cast<uint32_t>(id);
        }
        return written;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void* GetServerPartyTable() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerOffsetPlayer);
        if (!serverApp) return nullptr;
        // CServerExoApp facade → CServerExoAppInternal at +0x4 (mirrors
        // the CClientExoApp / *Internal split). The party_table is
        // embedded inside the internal at +0x1b770.
        void* serverInternal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverApp) +
            kServerExoAppInternalOffset);
        if (!serverInternal) return nullptr;
        return reinterpret_cast<unsigned char*>(serverInternal) +
               kServerInternalPartyTableOffset;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetSoloMode() {
    void* table = GetServerPartyTable();
    if (!table) return false;
    __try {
        return *reinterpret_cast<uint32_t*>(
                   reinterpret_cast<unsigned char*>(table) +
                   kPartyTableSoloModeOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

typedef int  (__thiscall* PFN_PartyTableGetIsNPCAvailable)(void*, int);
typedef char (__thiscall* PFN_PartyTableGetNPCSelectability)(void*, int);
typedef int  (__thiscall* PFN_PartyTableGetNPCObject)(void*, int, int, int);

}  // namespace

bool PartyTableIsNPCAvailable(int npcSlot) {
    if (npcSlot < 0 || npcSlot >= kPartyRosterSlotCount) return false;
    void* table = GetServerPartyTable();
    if (!table) return false;
    __try {
        auto fn = reinterpret_cast<PFN_PartyTableGetIsNPCAvailable>(
            kAddrCSWPartyTableGetIsNPCAvailable);
        return fn(table, npcSlot) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PartyTableIsNPCSelectable(int npcSlot) {
    if (npcSlot < 0 || npcSlot >= kPartyRosterSlotCount) return false;
    void* table = GetServerPartyTable();
    if (!table) return false;
    __try {
        auto fn = reinterpret_cast<PFN_PartyTableGetNPCSelectability>(
            kAddrCSWPartyTableGetNPCSelectability);
        return fn(table, npcSlot) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Roster name fallback. KOTOR 1's NPC slot index is fixed and these are
// proper nouns ("Bastila", "Carth Onasi", …) that don't change between
// English and German installs, so a hardcoded table is the simplest
// reliable path when GetNPCObject can't resolve a live creature (i.e.
// the companion is recruited but not in the current module — the open-
// world PartySelection case). Order matches the engine's roster index:
//   0 Bastila, 1 Canderous, 2 Carth, 3 HK-47, 4 Jolee, 5 Juhani,
//   6 Mission, 7 T3-M4, 8 Zaalbar.
static const char* const kCompanionNamesBySlot[kPartyRosterSlotCount] = {
    "Bastila Shan",
    "Canderous Ordo",
    "Carth Onasi",
    "HK-47",
    "Jolee Bindo",
    "Juhani",
    "Mission Vao",
    "T3-M4",
    "Zaalbar",
};

bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    if (npcSlot < 0 || npcSlot >= kPartyRosterSlotCount) return false;
    void* table = GetServerPartyTable();
    if (table) {
        uint32_t handle = 0;
        __try {
            auto fn = reinterpret_cast<PFN_PartyTableGetNPCObject>(
                kAddrCSWPartyTableGetNPCObject);
            // OnPanelAdded calls GetNPCObject(slot, 0, 1) first; if that's
            // 0 (creature not in the active module) it tries (slot, 1, 1).
            // Mirror that fallback so a name resolves even when the engine
            // had to fall through to the second-instance pool.
            int id0 = fn(table, npcSlot, 0, 1);
            if (id0 != 0) {
                handle = static_cast<uint32_t>(id0);
            } else {
                int id1 = fn(table, npcSlot, 1, 1);
                if (id1 != 0) handle = static_cast<uint32_t>(id1);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            handle = 0;
        }
        if (handle != 0) {
            // Path A: client-side universal display-name accessor. Resolves
            // in-module companions on the normal PartySelection screen.
            if (GetObjectDisplayNameByHandle(handle, outBuf, bufSize) &&
                outBuf[0] != '\0') {
                return true;
            }
            // Path B: the client namespace can't see this handle. On the Endar
            // Spire the slot-0 occupant is Trask — a live SERVER object whose
            // client-side name won't resolve (GetNPCObject hands back its
            // server id). Resolve the server creature and name it: prefer
            // stats.first_name via ExtractTextOrStrRef (a populated, localized
            // name where present), else the universal accessor. For this Trask
            // instance first_name is empty, so we land on his tag ("end_trask")
            // — still the real occupant, and preferable to the fixed-roster
            // "Bastila" the table below would otherwise emit.
            outBuf[0] = '\0';
            void* serverObj = ResolveServerObjectHandle(handle);
            if (serverObj) {
                void* stats = nullptr;
                __try {
                    stats = *reinterpret_cast<void**>(
                        reinterpret_cast<unsigned char*>(serverObj) +
                        kCreatureStatsPtrOffset);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    stats = nullptr;
                }
                if (stats &&
                    ExtractTextOrStrRef(stats, kCreatureStatsFirstNameOffset,
                                        kCreatureStatsFirstNameOffset + 4,
                                        outBuf, bufSize) &&
                    outBuf[0] != '\0') {
                    return true;
                }
                outBuf[0] = '\0';
                if (GetObjectName(serverObj, outBuf, bufSize) &&
                    outBuf[0] != '\0') {
                    return true;
                }
            }
        }
        outBuf[0] = '\0';
    }
    // Engine path didn't resolve — most often because the companion is on
    // the roster but not in the current module (open-world PartySelection
    // screen, away from base). Fall back to the fixed-roster table.
    const char* fixed = kCompanionNamesBySlot[npcSlot];
    if (!fixed || !fixed[0]) return false;
    size_t nlen = strlen(fixed);
    if (nlen + 1 > bufSize) return false;
    memcpy(outBuf, fixed, nlen + 1);
    return true;
}

}  // namespace acc::engine
