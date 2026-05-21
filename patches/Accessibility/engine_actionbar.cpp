#include "engine_actionbar.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "engine_offsets.h"
#include "engine_player.h"   // kAddrAppManagerPtr / kAppManagerClientAppOffset /
                             // kClientExoAppInternalOffset
#include "log.h"

namespace {

// CGuiInGame.main_interface offset — same as engine_picker / engine_radial.
constexpr size_t kGuiInGameMainInterfaceOffset = 0x90;

// CClientExoAppInternal.gui_in_game offset — same as engine_picker.
constexpr size_t kInternalGuiInGameOffset = 0x040;

// CSWGuiMainInterface.field45_0x771c[6] — kept only as the array base
// for OnAction*ArrowPressed slot identification (the engine handlers
// resolve column index from pointer math against this base). The
// embedded button structure within the column is ignored after the
// 2026-05-05 in-game test established the widgets are uninitialised.
constexpr size_t kColumnArrayOffset    = 0x771C;
constexpr size_t kColumnStride         = 0x71C;
constexpr size_t kColumnUpButtonOffset = 0x388;  // pointer-identity for handler
constexpr size_t kColumnDnButtonOffset = 0x54C;  // pointer-identity for handler

// CSWGuiMainInterface.field5_0x74[6] — six CExoArrayList<CSWGuiInterfaceAction>.
// Verified populated 2026-05-05: slot 1 reported size=2 matching the two
// medikits in inventory.
constexpr size_t kPersonalListsOffset    = 0x74;
constexpr size_t kPersonalListStride     = 0x0C;
constexpr size_t kPersonalListDataOffset = 0x00;  // T** data
constexpr size_t kPersonalListSizeOffset = 0x04;  // int size

// CSWGuiInterfaceAction layout — same as engine_radial / engine_picker.
constexpr size_t kIfActionLabelOffset    = 0x00;  // CExoString
constexpr size_t kIfActionIdOffset       = 0x08;  // ulong
constexpr size_t kIfActionStride         = 0x38;

// Engine entry points (verified from k1_win_gog_swkotor.exe.xml +
// docs/action-menu-investigation.md). GoG bytes match Steam per memory
// project_ghidra_gog_steam_bytes_match.
constexpr uintptr_t kAddrOnActionUpArrowPressed   = 0x0068af70;
constexpr uintptr_t kAddrOnActionDownArrowPressed = 0x0068afe0;
constexpr uintptr_t kAddrDoPersonalAction         = 0x0068ad60;

// CGuiInGame::SetMainInterfaceTarget @ 0x0062b000 — same wrapper as
// the radial/picker drive uses. Thin forwarder to
// CSWGuiMainInterface::SetTarget (stores field1_0x64 + resets the
// refresh-hint float field21_0x5cb0).
constexpr uintptr_t kAddrSetMainInterfaceTarget    = 0x0062b000;

// CGuiInGame::RePopulateMainInterface @ 0x0062b050 — thin forwarder to
// CSWGuiMainInterface::PopulateMenus @ 0x00689d80. Refreshes both the
// six personal-action lists (field5_0x74[0..5] via GetPersonalActions)
// and target_action_menu.action_lists[0..2] (via GetTargetActions for
// each row) against the currently-stamped main-interface target.
constexpr uintptr_t kAddrRePopulateMainInterface   = 0x0062b050;

// Function pointer types. Both arrow handlers take a single arg per
// the SARIF signature; OnActionUp expects CSWGuiControl* (the source
// button), OnActionDown is annotated `(int param_1)` but lives in the
// same family — almost certainly the same convention.
typedef void (__thiscall* PFN_OnActionArrowPressed)(void* this_, void* btn);
typedef void (__thiscall* PFN_DoPersonalAction)(void* this_,
                                                int slot, int param_2);
typedef void (__thiscall* PFN_SetMainInterfaceTarget)(void* this_,
                                                      uint32_t target);
typedef void (__thiscall* PFN_RePopulateMainInterface)(void* this_);

// Local chain helpers (same shape as engine_radial / engine_picker).
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

void* GetClientExoAppInternal(void* exoApp) {
    if (!exoApp) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetGuiInGame(void* internal) {
    if (!internal) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kInternalGuiInGameOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetMainInterface(void* guiInGame) {
    if (!guiInGame) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(guiInGame) +
            kGuiInGameMainInterfaceOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadInt32(void* base, size_t offset, int32_t* out) {
    if (!base || !out) return false;
    __try {
        *out = *reinterpret_cast<int32_t*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ReadPtr(void* base, size_t offset) {
    if (!base) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Address of column[slot] in the field45_0x771c array. Used as the
// pointer-identity argument for OnAction*ArrowPressed handlers — they
// recover the slot index via pointer math against the array base, so
// the synthetic offset works even when the embedded widgets within
// the column aren't fully initialised.
void* ColumnAddr(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= acc::engine_actionbar::kColumnCount) {
        return nullptr;
    }
    return reinterpret_cast<unsigned char*>(mi) +
           kColumnArrayOffset + slot * kColumnStride;
}

// Address of the descriptor list entry at `index` within slot's
// personal-actions list. Returns null on null/oor/empty list.
void* DescriptorAddr(void* mi, int slot, int index) {
    if (!mi || slot < 0 || slot >= acc::engine_actionbar::kColumnCount) {
        return nullptr;
    }
    if (index < 0) return nullptr;

    size_t listBase = kPersonalListsOffset + slot * kPersonalListStride;
    void* dataPtr   = ReadPtr(mi, listBase + kPersonalListDataOffset);
    int32_t size    = 0;
    ReadInt32(mi, listBase + kPersonalListSizeOffset, &size);
    if (!dataPtr || index >= size) return nullptr;

    return reinterpret_cast<unsigned char*>(dataPtr) +
           index * kIfActionStride;
}

// CExoString (c_string + length) read with NUL-termination guarantee.
bool ReadCExoStringLocal(void* base, size_t offset,
                         char* outBuf, size_t bufSize) {
    if (!base || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        auto* es = reinterpret_cast<CExoString*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        if (!es->c_string) return false;
        size_t i = 0;
        for (; i + 1 < bufSize; ++i) {
            char c = es->c_string[i];
            outBuf[i] = c;
            if (c == '\0') return i > 0;
        }
        outBuf[i] = '\0';
        return i > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
}

}  // namespace

namespace acc::engine_actionbar {

void* ResolveMainInterface() {
    void* exoApp   = GetClientExoApp();
    void* internal = GetClientExoAppInternal(exoApp);
    void* guiIn    = GetGuiInGame(internal);
    return GetMainInterface(guiIn);
}

int VariantCount(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= kColumnCount) return 0;
    int32_t size = 0;
    size_t off = kPersonalListsOffset + slot * kPersonalListStride +
                 kPersonalListSizeOffset;
    if (!ReadInt32(mi, off, &size)) return 0;
    if (size < 0) return 0;
    return static_cast<int>(size);
}

bool ReadVariantLabel(void* mi, int slot, int index,
                      char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    void* desc = DescriptorAddr(mi, slot, index);
    if (!desc) return false;
    return ReadCExoStringLocal(desc, kIfActionLabelOffset, outBuf, bufSize);
}

uint32_t ReadVariantActionId(void* mi, int slot, int index) {
    void* desc = DescriptorAddr(mi, slot, index);
    if (!desc) return 0;
    int32_t v = 0;
    ReadInt32(desc, kIfActionIdOffset, &v);
    return static_cast<uint32_t>(v);
}

bool CycleNextVariant(void* mi, int slot) {
    void* col = ColumnAddr(mi, slot);
    if (!col) return false;
    void* btn = reinterpret_cast<unsigned char*>(col) + kColumnUpButtonOffset;
    __try {
        auto fn = reinterpret_cast<PFN_OnActionArrowPressed>(
            kAddrOnActionUpArrowPressed);
        fn(mi, btn);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar", "CycleNextVariant SEH-FAULT slot=%d", slot);
        return false;
    }
}

bool CyclePrevVariant(void* mi, int slot) {
    void* col = ColumnAddr(mi, slot);
    if (!col) return false;
    void* btn = reinterpret_cast<unsigned char*>(col) + kColumnDnButtonOffset;
    __try {
        auto fn = reinterpret_cast<PFN_OnActionArrowPressed>(
            kAddrOnActionDownArrowPressed);
        fn(mi, btn);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar", "CyclePrevVariant SEH-FAULT slot=%d", slot);
        return false;
    }
}

bool FireSelectedVariant(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= kColumnCount) return false;
    __try {
        auto fn = reinterpret_cast<PFN_DoPersonalAction>(
            kAddrDoPersonalAction);
        // param_2 = 0 — initial guess; the engine-native bare-press path
        // also eventually reaches DoPersonalAction and that path works,
        // so 0 is plausibly "use the column's currently-selected variant".
        fn(mi, slot, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar", "FireSelectedVariant SEH-FAULT slot=%d", slot);
        return false;
    }
}

void LogState(void* mi, const char* tag) {
    const char* t = tag ? tag : "?";
    if (!mi) {
        acclog::Write("ActionBar.State", "[%s] main_interface=NULL", t);
        return;
    }
    for (int s = 0; s < kColumnCount; ++s) {
        int nVar = VariantCount(mi, s);
        char first[96] = "";
        ReadVariantLabel(mi, s, 0, first, sizeof(first));
        uint32_t firstId = ReadVariantActionId(mi, s, 0);
        acclog::Write("ActionBar.State",
            "[%s] col[%d] variants=%d data[0].label=[%s] data[0].action_id=0x%x",
            t, s, nVar, first, firstId);
    }
}

bool PrepareBareDispatch(uint32_t targetClientHandle) {
    void* exoApp   = GetClientExoApp();
    void* internal = GetClientExoAppInternal(exoApp);
    void* guiIn    = GetGuiInGame(internal);
    if (!guiIn) {
        acclog::Write("ActionBar.Prep",
            "chain unresolved (exoApp=%p internal=%p guiIn=%p) target=0x%08x",
            exoApp, internal, guiIn, targetClientHandle);
        return false;
    }
    __try {
        auto setTgt = reinterpret_cast<PFN_SetMainInterfaceTarget>(
            kAddrSetMainInterfaceTarget);
        setTgt(guiIn, targetClientHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar.Prep",
            "SetMainInterfaceTarget SEH-FAULT target=0x%08x",
            targetClientHandle);
        return false;
    }
    __try {
        auto repop = reinterpret_cast<PFN_RePopulateMainInterface>(
            kAddrRePopulateMainInterface);
        repop(guiIn);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar.Prep",
            "RePopulateMainInterface SEH-FAULT target=0x%08x",
            targetClientHandle);
        return false;
    }
    acclog::Write("ActionBar.Prep",
        "target=0x%08x — SetTarget + RePopulate done", targetClientHandle);
    return true;
}

}  // namespace acc::engine_actionbar
