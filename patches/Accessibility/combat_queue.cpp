#include "combat_queue.h"

#include <cstdint>
#include <cstdio>

#include "engine_area.h"      // ResolveServerObjectHandle, GetObjectName
#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_player.h"    // GetPlayerServerCreature
#include "hotkeys.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::combat::queue {

namespace {

// ---------------------------------------------------------------------------
// Engine surface — combat round + linked-list walker.
// ---------------------------------------------------------------------------

typedef int  (__thiscall* PFN_RemoveLastAction)(void* combatRound);
typedef void (__thiscall* PFN_ClearAllQueuedCombatActions)(void* clientObj);

constexpr uintptr_t kAddrCombatRoundRemoveLastAction =
    0x004d37b0;  // CSWSCombatRound::RemoveLastAction
constexpr uintptr_t kAddrCSWCObjectClearAllQueuedCombatActions =
    0x0063d490;  // CSWCObject::ClearAllQueuedCombatActions (client-side wipe)

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
// **Skeleton:** the numeric enum values inferred from the AddX adder
// declaration order in docs/combat-system.md were WRONG (validated
// 2026-05-10 from in-game observations: type=1 mapped to SpellCast was
// actually a basic attack — the user heard "Macht einsetzen" while in
// the tutorial level with no Force powers; type=11 was unmapped). Until
// a probe session pins the real enum, return QueueVerbUnknown
// ("Aktion") for everything to avoid speaking actively misleading
// verbs. The user can still see WHAT they queued via the target name +
// position; they just don't get an action-kind word.
//
// Once the enum is validated, restore the kActionType* case mappings
// (or replace this lookup with a per-row icon-resref read via
// GetActionIcon @0x686fb0 which gives a visible "iact_attack" /
// "iact_cast_spell" string straight from the engine).
acc::strings::Id VerbForActionType(unsigned char actionType) {
    (void)actionType;
    return acc::strings::Id::QueueVerbUnknown;
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

// Speak the focused row at index `idx` (0-based) of `count` total.
void SpeakRow(void* combatRound, int idx, int count) {
    void* action = GetQueueAction(combatRound, idx);
    unsigned char type = 0xff;
    uint32_t target = 0;
    ReadActionFields(action, type, target);

    const char* verb = acc::strings::Get(VerbForActionType(type));
    char tgtName[64] = "";
    if (target != 0u && target != 0x7F000000u) {
        // Try the engine's universal display-name accessor first — it
        // returns the localized name (e.g. "Sith-Soldat") even for
        // generic enemies whose `first_name` strref is empty (the user-
        // observed "end_cut2_sith1"-style tags came from the
        // GetObjectName tag fallback).
        if (!acc::engine::GetObjectDisplayNameByHandle(
                target, tgtName, sizeof(tgtName)) ||
            tgtName[0] == '\0') {
            void* tgtObj = acc::engine::ResolveServerObjectHandle(target);
            if (tgtObj) {
                acc::engine::GetObjectName(tgtObj, tgtName, sizeof(tgtName));
            }
        }
    }
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtQueueRow),
                  verb, tgtName, idx + 1, count);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.Queue", "row %d/%d type=%u target=0x%08x [%s]",
                  idx + 1, count, (unsigned)type, target, msg);
}

// Try to remove the action at `index`. Returns true on success.
//
// Engine surface caveat (docs/combat-system.md Phase 3 "Clear one"):
// only RemoveLastAction is exposed by name. As a skeleton, the only
// reliably-removable index is the last one. For other indices we return
// false; the user hears QueueRemoveFailed.
bool RemoveActionAtIndex(void* combatRound, void* clientCreature,
                         int index, int count) {
    if (!combatRound || count <= 0) return false;
    if (index != count - 1) {
        // Skeleton limitation — we only know the tail-remove primitive.
        // A real implementation either splices the linked list manually
        // or repeat-RemoveLast + re-queues the tail.
        acclog::Write("Combat.Queue",
                      "remove index=%d count=%d -> non-tail removal not "
                      "implemented yet",
                      index, count);
        return false;
    }
    __try {
        auto fn = reinterpret_cast<PFN_RemoveLastAction>(
            kAddrCombatRoundRemoveLastAction);
        fn(combatRound);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    (void)clientCreature;  // reserved for the future client-side wipe path
}

// Wipe the entire queue via the client-side ClearAllQueuedCombatActions.
// Falls back to repeat-RemoveLast if the client wipe faults.
bool ClearAllActions(void* combatRound, void* serverCreature) {
    // Find the matching client creature — chain is server +0xf8 not used
    // here; the reverse direction needs a CGameObjectArray client-side
    // resolve. For the skeleton, prefer the server-side RemoveLastAction
    // loop which we already have.
    int count = CountQueueEntries(combatRound);
    int removed = 0;
    for (int i = 0; i < count; ++i) {
        __try {
            auto fn = reinterpret_cast<PFN_RemoveLastAction>(
                kAddrCombatRoundRemoveLastAction);
            fn(combatRound);
            ++removed;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
    acclog::Write("Combat.Queue", "ClearAllActions removed=%d/%d",
                  removed, count);
    (void)serverCreature;
    return removed > 0;
}

// ---------------------------------------------------------------------------
// State machine.
// ---------------------------------------------------------------------------

struct State {
    bool  active   = false;
    int   focusIdx = 0;     // 0-based focus into the queue
};

State g_state;

// Bind the active session's creature pointer at Open time so per-tick
// auto-disarm can detect a leader swap.
void* g_boundCreature = nullptr;

}  // namespace

bool IsActive() { return g_state.active; }

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("Combat.Queue", "disarm reason=%s",
                  reason ? reason : "?");
    g_state.active   = false;
    g_state.focusIdx = 0;
    g_boundCreature  = nullptr;
}

bool Open() {
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        acclog::Write("Combat.Queue",
                      "Open — no player creature; not arming");
        return false;
    }
    void* round = ReadCombatRound(creature);
    if (!round) {
        acclog::Write("Combat.Queue",
                      "Open — no combat_round on creature=%p; not arming",
                      creature);
        // Speak the empty cue regardless so the user knows the keypress
        // landed.
        tolk::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/true);
        return false;
    }

    int count = CountQueueEntries(round);
    if (count <= 0) {
        tolk::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/true);
        acclog::Write("Combat.Queue",
                      "Open — queue empty creature=%p; not arming",
                      creature);
        return false;
    }

    g_state.active   = true;
    g_state.focusIdx = 0;
    g_boundCreature  = creature;

    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtQueueOpen),
                  count);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("Combat.Queue",
                  "ARMED creature=%p round=%p count=%d -> [%s]",
                  creature, round, count, msg);

    SpeakRow(round, 0, count);
    return true;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;
    if (value == 0) return false;  // press-edge only

    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature) {
        ForceDisarm("creature-unresolved");
        return false;
    }
    if (creature != g_boundCreature) {
        ForceDisarm("creature-changed");
        return false;
    }
    void* round = ReadCombatRound(creature);
    if (!round) {
        ForceDisarm("round-unresolved");
        return false;
    }
    int count = CountQueueEntries(round);
    if (count <= 0) {
        // Queue drained while submenu was open.
        tolk::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/true);
        ForceDisarm("queue-emptied");
        return true;
    }
    if (g_state.focusIdx >= count) g_state.focusIdx = count - 1;
    if (g_state.focusIdx < 0)      g_state.focusIdx = 0;

    switch (code) {
        case kInputNavUp:
            if (g_state.focusIdx > 0) --g_state.focusIdx;
            SpeakRow(round, g_state.focusIdx, count);
            return true;
        case kInputNavDown:
            if (g_state.focusIdx + 1 < count) ++g_state.focusIdx;
            SpeakRow(round, g_state.focusIdx, count);
            return true;
        case kInputEnter1:
        case kInputEnter2: {
            // Shift gate: if Shift is held, treat as "clear all". The
            // manager-level event doesn't surface modifier state, so we
            // read it directly via the central registry's ShiftHeld()
            // helper (covers L/R/either shift).
            bool shift = acc::hotkeys::ShiftHeld();
            if (shift) {
                bool ok = ClearAllActions(round, creature);
                tolk::Speak(acc::strings::Get(
                                ok ? acc::strings::Id::QueueCleared
                                   : acc::strings::Id::QueueRemoveFailed),
                            /*interrupt=*/true);
                acclog::Write("Combat.Queue", "Shift+Enter clear-all ok=%d",
                              ok ? 1 : 0);
                ForceDisarm("clear-all");
                return true;
            }

            // Single-row remove. Lookup verb before the remove for the
            // confirmation phrase.
            void* action = GetQueueAction(round, g_state.focusIdx);
            unsigned char type = 0xff;
            uint32_t      target = 0;
            ReadActionFields(action, type, target);
            const char* verb = acc::strings::Get(VerbForActionType(type));

            bool ok = RemoveActionAtIndex(round, creature,
                                          g_state.focusIdx, count);
            if (!ok) {
                tolk::Speak(acc::strings::Get(
                                acc::strings::Id::QueueRemoveFailed),
                            /*interrupt=*/true);
                return true;
            }
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          acc::strings::Get(
                              acc::strings::Id::FmtQueueRemoved),
                          verb);
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write("Combat.Queue",
                          "Removed idx=%d/%d type=%u verb=[%s]",
                          g_state.focusIdx + 1, count, (unsigned)type, verb);

            // Refresh queue + focus; if empty close.
            int newCount = CountQueueEntries(round);
            if (newCount <= 0) {
                ForceDisarm("queue-emptied-after-remove");
                return true;
            }
            if (g_state.focusIdx >= newCount) {
                g_state.focusIdx = newCount - 1;
            }
            SpeakRow(round, g_state.focusIdx, newCount);
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            tolk::Speak(acc::strings::Get(acc::strings::Id::QueueClosed),
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
    void* creature = acc::engine::GetPlayerServerCreature();
    if (!creature || creature != g_boundCreature) {
        ForceDisarm("tick-creature-drift");
        return;
    }
    void* round = ReadCombatRound(creature);
    if (!round) {
        ForceDisarm("tick-round-gone");
        return;
    }
    int count = CountQueueEntries(round);
    if (count <= 0) {
        // Queue drained organically (engine processed all actions).
        tolk::Speak(acc::strings::Get(acc::strings::Id::QueueEmpty),
                    /*interrupt=*/false);
        ForceDisarm("tick-queue-empty");
        return;
    }
}

void PollWin32Hotkey() {
    // Default open hotkey: Shift+K (Action::CombatQueueOpen). Wakes Open();
    // rest of the input routing happens through interact_hotkey.cpp's
    // submenu-active dispatch (same shape as actionbar_menu).
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::CombatQueueOpen)) return;

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    if (!Open()) return;
}

}  // namespace acc::combat::queue
