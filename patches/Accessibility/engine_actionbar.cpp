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

// CSWGuiMainInterface column array. field45_0x771c[6] starts at +0x771C
// with stride 0x71C (sizeof CSWGuiMainInterfaceAction). Same primitive
// the radial wraps in target_actions[3].
constexpr size_t kColumnArrayOffset    = 0x771C;
constexpr size_t kColumnStride         = 0x71C;
constexpr size_t kColumnActionButton   = 0x000;
constexpr size_t kColumnActionLabel    = 0x1C4;
constexpr size_t kColumnUpButton       = 0x388;
constexpr size_t kColumnDownButton     = 0x54C;
constexpr size_t kColumnIsActionOffset = 0x718;

// CSWGuiMainInterface.field5_0x74[6] — six CExoArrayList<CSWGuiInterfaceAction>
// (data/size/capacity = 12 bytes per entry).
constexpr size_t kPersonalListsOffset = 0x74;
constexpr size_t kPersonalListStride  = 0x0C;
constexpr size_t kPersonalListSize    = 0x04;  // .size within CExoArrayList

// Vtable index for HandleInputEvent on every CSWGuiControl. Same constant
// menus.cpp's FireActivate uses.
constexpr int kVtableHandleInputEvent = 15;

typedef void (__thiscall* PFN_ControlHandleInputEvent)(
    void* this_, int code, int state);

// Local chain helpers. The engine_picker and engine_radial modules each
// have their own copies for the same reason: keeping the engine layer
// dependency-free so each module can resolve to its own concrete sub-
// interface (TAM vs. main-interface vs. internal) without cross-module
// coupling.
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

// gui_string read path mirroring engine_radial::ReadGuiStringLocal.
// CSWGuiButton.gui_string sits at +0x168; CAurGUIStringInternal.c_string
// at +0x14. Validate the vtable before deref to skip stale embedded
// buttons (CSWGuiInGameMenu's icon labels are the canonical case where
// gui_string is null on the embed but the surrounding container caches
// the rendered text — irrelevant here since action_button always has a
// live gui_string when the column is populated).
bool ReadGuiStringLocal(void* control, size_t guiStringPtrOffset,
                        char* outBuf, size_t bufSize) {
    if (!control || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        void* gs = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + guiStringPtrOffset);
        if (!gs) return false;
        uintptr_t vt = *reinterpret_cast<uintptr_t*>(gs);
        if (vt != kVtableCAurGUIStringInternal) return false;
        const char* cstr = *reinterpret_cast<const char**>(
            reinterpret_cast<unsigned char*>(gs) + kAurGuiStringCStrOffset);
        if (!cstr) return false;
        size_t i = 0;
        for (; i + 1 < bufSize; ++i) {
            char c = cstr[i];
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

// CExoString fallback. Same shape as engine_radial / engine_picker.
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

void* ColumnAddr(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= acc::engine_actionbar::kColumnCount) {
        return nullptr;
    }
    return reinterpret_cast<unsigned char*>(mi) +
           kColumnArrayOffset + slot * kColumnStride;
}

bool FireActivate(void* control) {
    if (!control) return false;
    __try {
        void** vtable = *reinterpret_cast<void***>(control);
        if (!vtable) return false;
        auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
            vtable[kVtableHandleInputEvent]);
        if (!fn) return false;
        // 0x27 = KEYBOARD_F1 = the engine's activate code (kInputActivate).
        // value=1 = press edge.
        fn(control, 0x27, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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

int IsColumnActive(void* mi, int slot) {
    void* col = ColumnAddr(mi, slot);
    if (!col) return 0;
    int32_t v = 0;
    if (!ReadInt32(col, kColumnIsActionOffset, &v)) return 0;
    return v;
}

int VariantCount(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= kColumnCount) return 0;
    int32_t size = 0;
    size_t off = kPersonalListsOffset + slot * kPersonalListStride +
                 kPersonalListSize;
    if (!ReadInt32(mi, off, &size)) return 0;
    if (size < 0) return 0;
    return static_cast<int>(size);
}

bool ReadColumnLabel(void* mi, int slot, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    void* col = ColumnAddr(mi, slot);
    if (!col) return false;
    void* btn = reinterpret_cast<unsigned char*>(col) + kColumnActionButton;
    if (ReadGuiStringLocal(btn, kButtonGuiStringPtrOffset,
                           outBuf, bufSize)) return true;
    if (ReadCExoStringLocal(btn, kButtonTextOffset, outBuf, bufSize)) return true;
    return false;
}

bool FireUpButton(void* mi, int slot) {
    void* col = ColumnAddr(mi, slot);
    if (!col) return false;
    return FireActivate(reinterpret_cast<unsigned char*>(col) +
                        kColumnUpButton);
}

bool FireDownButton(void* mi, int slot) {
    void* col = ColumnAddr(mi, slot);
    if (!col) return false;
    return FireActivate(reinterpret_cast<unsigned char*>(col) +
                        kColumnDownButton);
}

bool FireActionButton(void* mi, int slot) {
    void* col = ColumnAddr(mi, slot);
    if (!col) return false;
    return FireActivate(reinterpret_cast<unsigned char*>(col) +
                        kColumnActionButton);
}

void LogState(void* mi, const char* tag) {
    const char* t = tag ? tag : "?";
    if (!mi) {
        acclog::Write("ActionBar.State[%s]: main_interface=NULL", t);
        return;
    }
    for (int s = 0; s < kColumnCount; ++s) {
        char actBtn[96] = "";
        char actLbl[96] = "";
        char upBtn[96]  = "";
        char dnBtn[96]  = "";
        void* col = ColumnAddr(mi, s);
        if (col) {
            unsigned char* base = reinterpret_cast<unsigned char*>(col);
            ReadGuiStringLocal(base + kColumnActionButton,
                               kButtonGuiStringPtrOffset,
                               actBtn, sizeof(actBtn));
            ReadGuiStringLocal(base + kColumnActionLabel,
                               kButtonGuiStringPtrOffset,
                               actLbl, sizeof(actLbl));
            ReadGuiStringLocal(base + kColumnUpButton,
                               kButtonGuiStringPtrOffset,
                               upBtn, sizeof(upBtn));
            ReadGuiStringLocal(base + kColumnDownButton,
                               kButtonGuiStringPtrOffset,
                               dnBtn, sizeof(dnBtn));
        }
        int isAct  = IsColumnActive(mi, s);
        int nVar   = VariantCount(mi, s);
        acclog::Write(
            "ActionBar.State[%s]: col[%d] is_action=%d variants=%d "
            "action_button=[%s] action_label=[%s] up=[%s] down=[%s]",
            t, s, isAct, nVar, actBtn, actLbl, upBtn, dnBtn);
    }
}

}  // namespace acc::engine_actionbar
