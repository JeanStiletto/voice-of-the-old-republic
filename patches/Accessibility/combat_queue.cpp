#include "combat_queue.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // ResolveServerObjectHandle, GetObjectName,
                              // GetObjectDisplayNameByHandle
#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_player.h"    // GetPlayerServerCreature, GetPartyMembers,
                              // GetPlayerCharacterName
#include "hotkeys.h"
#include "log.h"
#include "same_name_suffix.h" // AppendSuffix for same-LocName disambiguator
#include "strings.h"
#include "prism.h"

namespace acc::combat::queue {

namespace {

// ---------------------------------------------------------------------------
// Engine surface — combat round + linked-list walker.
// ---------------------------------------------------------------------------

typedef int  (__thiscall* PFN_RemoveLastAction)(void* combatRound);

constexpr uintptr_t kAddrCombatRoundRemoveLastAction =
    0x004d37b0;  // CSWSCombatRound::RemoveLastAction

// Read CSWSCreature.combat_round @+0x9c8.
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

// CSWSCombatRound.actions is a CExoLinkedList<CSWSCombatRoundAction>*
// at +0x9b0. The list is a node-chain via `next` pointers; each node
// carries a CSWSCombatRoundAction* in its data slot. Walk to a fixed
// cap (we never expect more than ~16 queued actions in practice).
//
// Filter (validated 2026-05-10): the engine consistently has a leading
// node with action_type=255 (0xFF) and target=0 — appears in every
// queue dump regardless of creature. Likely the "current dispatching
// action" placeholder slot the engine maintains. Skipping these in
// both Count and GetQueueAction surfaces only the real queued entries
// to the user (matches what the sighted in-game UI shows).
constexpr int kMaxQueueWalk = 64;

bool ReadNodeActionType(void* node, unsigned char& outType) {
    if (!node) return false;
    __try {
        void* data = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(node) +
            kLinkedListNodeDataOff);
        if (!data) return false;
        outType = *(reinterpret_cast<unsigned char*>(data) +
                    kCombatRoundActionTypeOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int CountQueueEntries(void* combatRound) {
    if (!combatRound) return 0;
    int count = 0;
    __try {
        void* listPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(combatRound) +
            kCombatRoundActionsOffset);
        if (!listPtr) return 0;
        void* node = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(listPtr) +
            kLinkedListHeadOffset);
        int walked = 0;
        while (node && walked < kMaxQueueWalk) {
            ++walked;
            unsigned char t = 0;
            if (ReadNodeActionType(node, t) && t != 0xFF) {
                ++count;
            }
            node = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kLinkedListNodeNextOff);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return count;
}

// Get the action node at zero-based `index` (skipping placeholder
// type=0xFF entries), or nullptr if out of range.
void* GetQueueAction(void* combatRound, int index) {
    if (!combatRound || index < 0) return nullptr;
    __try {
        void* listPtr = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(combatRound) +
            kCombatRoundActionsOffset);
        if (!listPtr) return nullptr;
        void* node = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(listPtr) +
            kLinkedListHeadOffset);
        int matched = 0, walked = 0;
        while (node && walked < kMaxQueueWalk) {
            ++walked;
            unsigned char t = 0;
            if (ReadNodeActionType(node, t) && t != 0xFF) {
                if (matched == index) {
                    return *reinterpret_cast<void**>(
                        reinterpret_cast<unsigned char*>(node) +
                        kLinkedListNodeDataOff);
                }
                ++matched;
            }
            node = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kLinkedListNodeNextOff);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

// Map an action_type byte to a localised verb.
//
// Enum confirmed 2026-05-14 by decompiling CSWGuiMainInterface::GetActionIcon
// @0x686fb0 — the engine's own per-slot icon resolver. The switch in
// case 0xc (the standard combat-round path the strip uses) decodes:
//
//   action_type=1   → i_attack  / i_attackm   ("Bash door" is just an
//                                              attack with a door target)
//   action_type=6   → i_equip   / i_equipm
//   action_type=7   → i_unequip / i_equipm
//   action_type=9   → SpellArray lookup / i_powerm  (Cast Force Power)
//   action_type=10  → ItemArray lookup / i_useitemm (Use Item)
//   action_type=11  → FeatArray lookup / i_featm    (Use Feat — includes
//                                                    the player's
//                                                    Power Attack /
//                                                    Flurry / etc.)
//
// QueueVerbUseTalent serves as our "Use Feat" word since the existing
// table already groups feat/talent activations under that ID (TSL
// renamed them "talents" anyway, and the German "Talent einsetzen"
// reads correctly for both feats and force-power adjacent talents).
acc::strings::Id VerbForActionType(unsigned char actionType) {
    switch (actionType) {
        case 1:  return acc::strings::Id::QueueVerbAttack;
        case 6:  return acc::strings::Id::QueueVerbEquip;
        case 7:  return acc::strings::Id::QueueVerbUnequip;
        case 9:  return acc::strings::Id::QueueVerbCastForce;
        case 10: return acc::strings::Id::QueueVerbItemCast;
        case 11: return acc::strings::Id::QueueVerbUseTalent;
        default: return acc::strings::Id::QueueVerbUnknown;
    }
}

// Read action_type byte + target handle from a CSWSCombatRoundAction.
bool ReadActionFields(void* action, unsigned char& outType,
                      uint32_t& outTarget) {
    outType = 0xff;
    outTarget = 0;
    if (!action) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(action);
        outType   = *(base + kCombatRoundActionTypeOffset);
        outTarget = *reinterpret_cast<uint32_t*>(
            base + kCombatRoundActionTargetOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outType = 0xff;
        outTarget = 0;
        return false;
    }
}

// ---------------------------------------------------------------------------
// Flat row model.
// ---------------------------------------------------------------------------

// One row per queued action, across every party member's combat round.
// Built at Open() and rebuilt after every mutation (remove / clear).
//
// The party walk uses CSWPartyTable.pt_member_ids (engine_player.h).
// pt_member_ids[0] is the chargen PC (its display-name lookup returns
// empty so we fall back to GetPlayerCharacterName for that slot only).
struct Row {
    void*    creature;     // CSWSCreature* — owner of this action
    void*    combatRound;  // CSWSCombatRound* — for tail-remove dispatch
    int      perCreatureIdx;  // 0-based index of this row within the
                              // owner's local queue (used for the count
                              // we tail-remove against)
    int      perCreatureCount;  // total queued on this owner (snapshot)
    char     charName[64];
};

constexpr int kMaxRows = 32;  // 3 members × up to ~10 actions each, with
                              // headroom for transient additions

struct State {
    bool active   = false;
    int  focusIdx = 0;  // 0-based focus into rows[0..count)
    int  count    = 0;
    Row  rows[kMaxRows];
};

State g_state;

// Resolve the display name for a party member by handle.
// pt_member_ids[0] is the chargen PC — GetObjectDisplayNameByHandle
// returns the engine's universal name which is empty for the PC stats
// (see engine_player::GetPlayerCharacterName header for the reason).
// We pass isPC=true for that slot so the fallback hits the chargen
// name slot.
void ResolveMemberName(uint32_t handle, bool isPC,
                       char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return;
    outBuf[0] = '\0';
    if (!isPC) {
        if (acc::engine::GetObjectDisplayNameByHandle(handle, outBuf,
                                                     bufSize) &&
            outBuf[0] != '\0') {
            return;
        }
    }
    // PC slot, or display-name path returned empty — fall back to the
    // chargen-name accessor. This is the same fallback chain
    // GetActiveLeaderName uses for the controlled creature.
    acc::engine::GetPlayerCharacterName(outBuf, bufSize);
}

// Rebuild g_state.rows from live engine state. Returns the row count.
// Preserves the focus if possible (clamped to the new size).
int BuildRows() {
    g_state.count = 0;

    uint32_t handles[kPartyTableMaxMembers] = {};
    int      n = acc::engine::GetPartyMembers(
        handles, kPartyTableMaxMembers);

    // Fallback: if the party table is unreadable (early init, very
    // first beat of a new save), at least surface the controlled
    // creature's queue rather than going silent.
    if (n <= 0) {
        void* leader = acc::engine::GetPlayerServerCreature();
        if (!leader) return 0;
        void* round = ReadCombatRound(leader);
        if (!round) return 0;
        int local = CountQueueEntries(round);
        for (int i = 0; i < local && g_state.count < kMaxRows; ++i) {
            Row& r = g_state.rows[g_state.count++];
            r.creature = leader;
            r.combatRound = round;
            r.perCreatureIdx = i;
            r.perCreatureCount = local;
            r.charName[0] = '\0';
            acc::engine::GetActiveLeaderName(r.charName, sizeof(r.charName));
        }
        return g_state.count;
    }

    for (int m = 0; m < n; ++m) {
        uint32_t handle = handles[m];
        if (handle == 0u || handle == 0xFFFFFFFFu ||
            handle == 0x7F000000u) {
            continue;
        }
        void* creature = acc::engine::ResolveServerObjectHandle(handle);
        if (!creature) {
            // Some handles come from the engine in the client-side
            // namespace — fold through the client resolver, which
            // returns the server CSWSObject* directly.
            creature = acc::engine::ResolveClientObjectHandle(handle);
        }
        if (!creature) continue;
        void* round = ReadCombatRound(creature);
        if (!round) continue;
        int local = CountQueueEntries(round);
        if (local <= 0) continue;

        char name[64] = "";
        ResolveMemberName(handle, /*isPC=*/m == 0, name, sizeof(name));

        for (int i = 0; i < local && g_state.count < kMaxRows; ++i) {
            Row& r = g_state.rows[g_state.count++];
            r.creature = creature;
            r.combatRound = round;
            r.perCreatureIdx = i;
            r.perCreatureCount = local;
            std::strncpy(r.charName, name, sizeof(r.charName) - 1);
            r.charName[sizeof(r.charName) - 1] = '\0';
        }
    }
    return g_state.count;
}

// Speak the focused row at index `idx` (0-based) of `count` total.
void SpeakRow(int idx) {
    if (idx < 0 || idx >= g_state.count) return;
    const Row& row = g_state.rows[idx];
    void* action = GetQueueAction(row.combatRound, row.perCreatureIdx);

    unsigned char type = 0xff;
    uint32_t target = 0;
    ReadActionFields(action, type, target);

    const char* verb = acc::strings::Get(VerbForActionType(type));

    char tgtName[64] = "";
    if (target != 0u && target != 0x7F000000u) {
        if (!acc::engine::GetObjectDisplayNameByHandle(
                target, tgtName, sizeof(tgtName)) ||
            tgtName[0] == '\0') {
            void* tgtObj = acc::engine::ResolveServerObjectHandle(target);
            if (tgtObj) {
                acc::engine::GetObjectName(tgtObj, tgtName, sizeof(tgtName));
            }
        }
        if (tgtName[0] != '\0') {
            void* tgtObj = acc::engine::ResolveServerObjectHandle(target);
            if (tgtObj) {
                acc::narration::AppendSuffix(tgtObj, tgtName, sizeof(tgtName));
            }
        }
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtQueueRow),
                  row.charName, verb, tgtName, idx + 1, g_state.count);
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.Queue",
                  "row %d/%d char=[%s] type=%u target=0x%08x verb=[%s] "
                  "tgt=[%s]",
                  idx + 1, g_state.count, row.charName, (unsigned)type,
                  target, verb, tgtName);
}

// Try to remove the action at row `idx`. Tail-only — the engine's
// only public per-round primitive is RemoveLastAction. Returns true
// on dispatch success.
bool RemoveRow(int idx) {
    if (idx < 0 || idx >= g_state.count) return false;
    const Row& row = g_state.rows[idx];
    // Tail-only — see docs/combat-system.md Phase 3 "Clear one" item.
    // We compare against the per-creature index so that the user's
    // current row maps to the tail of *that creature's* queue (not
    // the flat-list tail).
    if (row.perCreatureIdx != row.perCreatureCount - 1) {
        acclog::Write("Combat.Queue",
                      "remove flat=%d char=[%s] local=%d/%d -> non-tail "
                      "remove not implemented",
                      idx, row.charName, row.perCreatureIdx,
                      row.perCreatureCount);
        return false;
    }
    __try {
        auto fn = reinterpret_cast<PFN_RemoveLastAction>(
            kAddrCombatRoundRemoveLastAction);
        fn(row.combatRound);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Clear every party member's queue via repeated RemoveLastAction calls
// against each row's combat round.
bool ClearAllRows() {
    int total = g_state.count;
    int removed = 0;
    // Iterate rows back-to-front so each per-creature tail-remove
    // matches the engine's only primitive.
    for (int i = total - 1; i >= 0; --i) {
        __try {
            auto fn = reinterpret_cast<PFN_RemoveLastAction>(
                kAddrCombatRoundRemoveLastAction);
            fn(g_state.rows[i].combatRound);
            ++removed;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
    acclog::Write("Combat.Queue", "ClearAllRows removed=%d/%d",
                  removed, total);
    return removed > 0;
}

}  // namespace

bool IsActive() { return g_state.active; }

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("Combat.Queue", "disarm reason=%s",
                  reason ? reason : "?");
    g_state.active = false;
    g_state.focusIdx = 0;
    g_state.count = 0;
}

bool Open() {
    int count = BuildRows();
    if (count <= 0) {
        prism::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/true);
        acclog::Write("Combat.Queue", "Open — party queue empty; not arming");
        return false;
    }

    g_state.active   = true;
    g_state.focusIdx = 0;

    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtQueueOpen),
                  count);
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.Queue", "ARMED rows=%d -> [%s]", count, msg);

    SpeakRow(0);
    return true;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;
    if (value == 0) return false;  // press-edge only

    // Refresh on every keypress — the engine can drain the queue between
    // ticks (combat round advancing) and we don't want to dispatch a
    // remove against a stale row.
    int count = BuildRows();
    if (count <= 0) {
        prism::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/true);
        ForceDisarm("queue-emptied");
        return true;
    }
    if (g_state.focusIdx >= count) g_state.focusIdx = count - 1;
    if (g_state.focusIdx < 0)      g_state.focusIdx = 0;

    switch (code) {
        case kInputNavUp:
            if (g_state.focusIdx > 0) --g_state.focusIdx;
            SpeakRow(g_state.focusIdx);
            return true;
        case kInputNavDown:
            if (g_state.focusIdx + 1 < count) ++g_state.focusIdx;
            SpeakRow(g_state.focusIdx);
            return true;
        case kInputEnter1:
        case kInputEnter2: {
            bool shift = acc::hotkeys::ShiftHeld();
            if (shift) {
                bool ok = ClearAllRows();
                prism::Speak(acc::strings::Get(
                                ok ? acc::strings::Id::QueueCleared
                                   : acc::strings::Id::QueueRemoveFailed),
                            /*interrupt=*/true);
                acclog::Write("Combat.Queue", "Shift+Enter clear-all ok=%d",
                              ok ? 1 : 0);
                ForceDisarm("clear-all");
                return true;
            }

            // Capture the verb before the remove so the confirmation
            // phrase still has it.
            const Row& row = g_state.rows[g_state.focusIdx];
            void* action = GetQueueAction(row.combatRound,
                                          row.perCreatureIdx);
            unsigned char type = 0xff;
            uint32_t target = 0;
            ReadActionFields(action, type, target);
            const char* verb = acc::strings::Get(VerbForActionType(type));

            bool ok = RemoveRow(g_state.focusIdx);
            if (!ok) {
                prism::Speak(acc::strings::Get(
                                acc::strings::Id::QueueRemoveFailed),
                            /*interrupt=*/true);
                return true;
            }
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          acc::strings::Get(
                              acc::strings::Id::FmtQueueRemoved),
                          verb);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("Combat.Queue",
                          "Removed flat=%d type=%u verb=[%s]",
                          g_state.focusIdx + 1, (unsigned)type, verb);

            int newCount = BuildRows();
            if (newCount <= 0) {
                ForceDisarm("queue-emptied-after-remove");
                return true;
            }
            if (g_state.focusIdx >= newCount) {
                g_state.focusIdx = newCount - 1;
            }
            SpeakRow(g_state.focusIdx);
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            prism::Speak(acc::strings::Get(acc::strings::Id::QueueClosed),
                        /*interrupt=*/true);
            acclog::Write("Combat.Queue", "Esc -> close");
            ForceDisarm("esc");
            return true;
        }
        default:
            return false;
    }
}

void Tick() {
    if (!g_state.active) return;
    int count = BuildRows();
    if (count <= 0) {
        prism::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/false);
        ForceDisarm("tick-queue-empty");
        return;
    }
}

void PollWin32Hotkey() {
    // Default open hotkey: Shift+K (Action::CombatQueueOpen).
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::CombatQueueOpen)) return;

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    if (!Open()) return;
}

}  // namespace acc::combat::queue
