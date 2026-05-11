#include "combat_query.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetObjectName, GetObjectKind, ResolveServerObjectHandle
#include "engine_manager.h"   // kAddrGuiManagerPtr
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKind / IdentifyPanel
#include "engine_player.h"    // GetPlayerServerCreature, GetActiveLeaderName
#include "engine_reads.h"
#include "hotkeys.h"
#include "log.h"
#include "menus_extract.h"    // FromControl — for Examine message-box read
#include "strings.h"
#include "tolk.h"

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

bool ReadStatSnap(void* serverCreature, StatSnap& out) {
    std::memset(&out, 0, sizeof(out));
    if (!serverCreature) return false;
    void* stats = ReadCreatureStats(serverCreature);
    if (!stats) return false;

    out.hpCur    = CallIntAccessor(serverCreature,
                                   kAddrCSWSObjectGetCurrentHitPoints);
    out.hpMax    = CallIntAccessor(serverCreature,
                                   kAddrCSWSCreatureGetMaxHitPoints);
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
        tolk::Speak(phrase, /*interrupt=*/true);
        acclog::Write("Combat.PcStat", "no creature -> [%s]", phrase);
        return false;
    }
    StatSnap snap;
    if (!ReadStatSnap(creature, snap)) {
        const char* phrase = acc::strings::Get(
            acc::strings::Id::PcStatNoCharacter);
        tolk::Speak(phrase, /*interrupt=*/true);
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

    tolk::Speak(msg, /*interrupt=*/true);
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
    tolk::Speak(now, /*interrupt=*/true);
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

    // Skeleton: only direct field reads (no suspected engine accessor
    // calls in the auto-firing cycle path — see combat.cpp's
    // ReadCreatureHpDirect rationale). HP-current via the documented
    // CSWSObject.hit_points @+0xe0 offset; max / AC / faction left for
    // the follow-up after accessor addresses are validated.
    int hpCur = 0;
    __try {
        hpCur = static_cast<int>(*reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(targetServerObject) +
            kObjectHitPointsOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hpCur = 0;
    }

    using S = acc::strings::Id;
    // Faction-relation classification is an "open" item — placeholder
    // "neutral" until CSWSFaction is decoded.
    const char* factionWord = acc::strings::Get(S::FactionNeutral);

    std::snprintf(outBuf, outBufSize,
                  acc::strings::Get(S::FmtTargetCombatBrief),
                  targetName ? targetName : "?",
                  factionWord, hpCur);
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
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Combat.Examine", "Shift+H -> no target [%s]", msg);
        return;
    }

    // Skeleton: skip CGuiInGame::ShowExamineBox entirely. The engine's
    // panel is server-driven (populated via
    // SendServerToPlayerExamineGui_CreatureData / ItemData / DoorData
    // / PlaceableData / MineData per object kind) and ShowExamineBox
    // is a 2-arg `void(ulong handle, int param_2)` whose param_2
    // semantics aren't documented (calling with 1 arg corrupted the
    // stack — verified 2026-05-10 panel-walk dump showed only stale
    // close-button text). Read stats directly instead.
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
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Combat.Examine",
                      "Shift+H -> handle 0x%08x failed name resolution [%s]",
                      handle, msg);
        return;
    }

    int kind = obj ? acc::engine::GetObjectKind(obj) : -1;
    char msg[512];
    if (obj && kind == static_cast<int>(acc::engine::GameObjectKind::Creature)) {
        // Direct field reads only — the suspected engine accessors
        // (GetMaxHitPoints / GetArmorClass / save accessors) aren't
        // safe to call from a hotkey path that the user might press
        // before they're validated. HP-current via the documented
        // CSWSObject.hit_points @+0xe0 offset.
        int hpCur = 0;
        __try {
            hpCur = static_cast<int>(*reinterpret_cast<short*>(
                reinterpret_cast<unsigned char*>(obj) +
                kObjectHitPointsOffset));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            hpCur = 0;
        }
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(
                          acc::strings::Id::FmtTargetCombatBrief),
                      name,
                      acc::strings::Get(acc::strings::Id::FactionNeutral),
                      hpCur);
    } else {
        // Non-creature target — speak the localized name as a starting
        // point. Door / placeable / item-specific enrichment is the
        // job of GetObjectName which is what we're calling already.
        std::snprintf(msg, sizeof(msg), "%s.", name);
    }
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.Examine",
                  "Shift+H direct read handle=0x%08x name=[%s] kind=%d -> [%s]",
                  handle, name, kind, msg);
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

    // The Examine panel layout (validated 2026-05-10 against
    // patch-20260510-000722.log:Menus.PanelWalk dump):
    //
    //   children=6
    //     [0] NULL
    //     [1] id=1   listbox (vtable=0073E840) — message_box content
    //     [2] NULL
    //     [3] id=3   button "Schliess." (Close)
    //     [4] id=4   button "Abbrechen" (Cancel)
    //     [5] id=-1  label-like control (vtable 0073E5B8)
    //
    // The actual examine content lives in the id=1 listbox (each line
    // of the rendered text is a row). Walk every row and concatenate;
    // this gives us the full stat block instead of a button label.
    //
    // Original skeleton walked panel.controls and grabbed the first
    // non-empty short string, which always landed on id=3 / id=4 / id=5
    // (the buttons) — observed text=[Schliess.] / [Abbrechen] /
    // [Laserschwert werfen].
    auto* pBase = reinterpret_cast<unsigned char*>(examinePanel);
    auto* controls = reinterpret_cast<CExoArrayList*>(
        pBase + kPanelControlsOffset);

    // Find the listbox child (vtable match — same identity test
    // engine_reads.cpp uses for IsListBox).
    void* listBox = nullptr;
    __try {
        if (controls && controls->data) {
            int sz = controls->size > 16 ? 16 : controls->size;
            for (int i = 0; i < sz; ++i) {
                void* child = controls->data[i];
                if (!child) continue;
                void** vt = *reinterpret_cast<void***>(child);
                if (reinterpret_cast<uintptr_t>(vt) == kVtableListBox) {
                    listBox = child;
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        listBox = nullptr;
    }

    char text[2048] = "";
    bool gotText = false;
    __try {
        if (listBox) {
            auto* lbList = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(listBox) +
                kListBoxControlsOffset);
            if (lbList && lbList->data && lbList->size > 0) {
                size_t off = 0;
                int rowCap = lbList->size > 32 ? 32 : lbList->size;
                for (int r = 0; r < rowCap; ++r) {
                    void* row = lbList->data[r];
                    if (!row) continue;
                    char rowText[256];
                    if (!acc::menus::extract::FromControl(
                            row, rowText, sizeof(rowText))) {
                        continue;
                    }
                    if (rowText[0] == '\0') continue;
                    int n = std::snprintf(text + off, sizeof(text) - off,
                                          "%s%s",
                                          off == 0 ? "" : ". ",
                                          rowText);
                    if (n > 0) off += static_cast<size_t>(n);
                    if (off >= sizeof(text)) break;
                }
                gotText = (off > 0);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gotText = false;
    }

    if (gotText) {
        tolk::Speak(text, /*interrupt=*/false);
        acclog::Write("Combat.Examine", "panel opened text=[%.300s]", text);
    } else {
        acclog::Write("Combat.Examine",
                      "panel opened, no listbox content yet (panel=%p lb=%p)",
                      examinePanel, listBox);
    }
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

}  // namespace acc::combat::query
