// KOTOR 2 Quick Menu navigator — see pad_quickmenu.h for the design.

#include "pad_quickmenu.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "engine_area.h"      // ResolveServerObjectHandle / GetObjectName
#include "engine_game.h"
#include "engine_offsets.h"
#include "engine_panels.h"    // ResolveMainInterface
#include "engine_player.h"    // GetPartyMembers
#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::pad::quickmenu {

namespace {

// ---------------------------------------------------------------------------
// Engine layout — KOTOR 2 only, single consumer, so declared here rather than
// in the shared offset headers (same arrangement engine_actionbar.cpp uses for
// the action-bar column fields).
//
//   CSWGuiMainInterface + 0x98        -> CSWGuiActionMenuIos*  (heap, 0x15418)
//   CSWGuiActionMenuIos + 0xd588      -> CSWGamepadMenuIos     (embedded)
//   CSWGamepadMenuIos   + 0x68        selected entry, 0..7; ctor writes -1
//                       + 0x6c        sub-entry index
//                       + 0x70        sub-entry count (0 = no sub-list open)
//
// Witnesses: ctor 0x0074a500 (writes +0x68 = -1, +0x74 = 0, +0x78 = owner and
// builds the four 0x1d0-byte widget arrays), input handler 0x00754950 (steps
// +0x68 modulo 8 and +0x6c within +0x70), layout/draw 0x00753d60.
// ---------------------------------------------------------------------------
const size_t kMainInterfaceActionMenuIosOffset = acc::off::Kotor2Only(0x98);
const size_t kActionMenuIosGamepadMenuOffset   = acc::off::Kotor2Only(0xd588);
const size_t kGamepadMenuSelectedOffset        = acc::off::Kotor2Only(0x68);
const size_t kGamepadMenuSubSelectedOffset     = acc::off::Kotor2Only(0x6c);
const size_t kGamepadMenuSubCountOffset        = acc::off::Kotor2Only(0x70);

constexpr int kEntryCount = 8;

// Fixed engine order. The comment in pad_quickmenu.h is the contract; this is
// it in code.
const acc::strings::Id kEntryLabels[kEntryCount] = {
    acc::strings::Id::PadQuickMenus,
    acc::strings::Id::PadQuickPartyLeader,
    acc::strings::Id::PadQuickSoloParty,
    acc::strings::Id::PadQuickStealth,
    acc::strings::Id::PadQuickSave,
    acc::strings::Id::PadQuickFreeLook,
    acc::strings::Id::PadQuickSwitchWeapons,
    acc::strings::Id::PadQuickHelp,
};

bool      g_armed       = false;
int       g_lastEntry   = -2;  // -2 = nothing spoken yet this time round
int       g_lastSub     = -2;
bool      g_spokeOpener = false;
ULONGLONG g_armedAtMs   = 0;

// The Y press is what arms us, but the engine can refuse to OPEN the menu
// (its own open path checks the world state and a gate global). If the
// selection index never becomes valid, the arm has to expire — otherwise a
// refused open would leave the D-Pad standing down for the rest of the
// session, silently costing the player their cycle bindings.
constexpr ULONGLONG kOpenGraceMs = 1500;

// Resolve the embedded CSWGamepadMenuIos, or nullptr. Never caches: the
// action menu is heap-allocated and the main interface is rebuilt across
// module loads.
void* Resolve() {
    if (!acc::game::IsKotor2()) return nullptr;
    void* mi = acc::engine::ResolveMainInterface();
    if (!mi) return nullptr;
    void* actionMenu = nullptr;
    __try {
        actionMenu = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(mi) +
            kMainInterfaceActionMenuIosOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    // Interior pointer into a heap object — acc::off::Ptr converts an unported
    // offset into an honest nullptr instead of a wild-but-non-null pointer.
    return acc::off::Ptr(actionMenu, kActionMenuIosGamepadMenuOffset);
}

bool ReadState(void* menu, int& entry, int& sub, int& subCount) {
    if (!menu) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(menu);
        entry    = *reinterpret_cast<int*>(base + kGamepadMenuSelectedOffset);
        sub      = *reinterpret_cast<int*>(base + kGamepadMenuSubSelectedOffset);
        subCount = *reinterpret_cast<int*>(base + kGamepadMenuSubCountOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Party-slot name for the Party Leader sub-list. The engine expands entry 1
// into three items, one per party slot; speaking the member's name is the only
// useful thing to say about a slot. Falls back to the ordinal when the roster
// cannot be read, per the never-silence-the-fallback rule.
void SpeakPartySlot(int slot) {
    char name[128] = "";
    uint32_t handles[16] = {};
    const int n = acc::engine::GetPartyMembers(handles, 16);
    if (slot >= 0 && slot < n) {
        if (void* obj = acc::engine::ResolveServerObjectHandle(handles[slot])) {
            acc::engine::GetObjectName(obj, name, sizeof(name));
        }
    }
    if (name[0] == '\0') {
        std::snprintf(name, sizeof(name),
                      acc::strings::Get(acc::strings::Id::FmtPadQuickSlot),
                      slot + 1);
    }
    prism::Speak(name, /*interrupt=*/true);
    acclog::Write("Pad.QuickMenu", "sub=%d -> [%s]", slot, name);
}

void SpeakEntry(int entry) {
    const char* label =
        (entry >= 0 && entry < kEntryCount)
            ? acc::strings::Get(kEntryLabels[entry])
            : acc::strings::Get(acc::strings::Id::PadQuickUnknownEntry);
    prism::Speak(label, /*interrupt=*/true);
    acclog::Write("Pad.QuickMenu", "entry=%d -> [%s]", entry, label);
}

}  // namespace

bool IsArmed() { return g_armed; }

void NoteOpened() {
    if (!acc::game::IsKotor2()) return;
    g_armed       = true;
    g_lastEntry   = -2;
    g_lastSub     = -2;
    g_spokeOpener = false;
    g_armedAtMs   = GetTickCount64();
    acclog::Write("Pad.QuickMenu", "armed (Y pressed)");
}

void NoteClosed() {
    if (!g_armed) return;
    g_armed = false;
    acclog::Write("Pad.QuickMenu", "disarmed");
}

void Tick() {
    if (!g_armed) return;

    // The menu only exists in the world. An area load or a teardown under an
    // armed menu must not leave the arm standing — it gates the D-Pad.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write("Pad.QuickMenu", "world gone — disarming");
        NoteClosed();
        return;
    }

    void* menu = Resolve();
    int entry = -1, sub = -1, subCount = 0;
    if (!ReadState(menu, entry, sub, subCount)) {
        // Cannot see the menu any more — the world was torn down under it, or
        // the main interface is being rebuilt. Stand down rather than hold a
        // stale arm across an area load.
        acclog::Write("Pad.QuickMenu", "state unreadable — disarming");
        NoteClosed();
        return;
    }

    // The engine's ctor parks the index at -1 and the open path moves it onto
    // a real entry. Stay quiet until it does — but not forever: see
    // kOpenGraceMs.
    if (entry < 0) {
        if (!g_spokeOpener && g_armedAtMs != 0 &&
            (GetTickCount64() - g_armedAtMs) > kOpenGraceMs) {
            acclog::Write("Pad.QuickMenu",
                          "engine never opened the menu — disarming");
            NoteClosed();
        }
        return;
    }

    // First sight: title, then the focused entry. Never the whole list — the
    // mod's first-sight rule everywhere else.
    if (!g_spokeOpener) {
        prism::Speak(acc::strings::Get(acc::strings::Id::PadQuickMenuOpened),
                     /*interrupt=*/true);
        g_spokeOpener = true;
        g_lastEntry = -2;   // force the entry announce below
    }

    const bool inSubList = (subCount > 0);
    if (inSubList) {
        if (sub != g_lastSub || entry != g_lastEntry) {
            SpeakPartySlot(sub);
            g_lastSub   = sub;
            g_lastEntry = entry;
        }
        return;
    }

    g_lastSub = -2;   // left the sub-list; next entry into it re-announces
    if (entry != g_lastEntry) {
        SpeakEntry(entry);
        g_lastEntry = entry;
    }
}

}  // namespace acc::pad::quickmenu
