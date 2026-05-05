#include "engine_radial.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_offsets.h"   // kButtonGuiStringPtrOffset,
                              // kButtonTextOffset, kLabelGuiStringPtrOffset,
                              // kLabelTextOffset, kAurGuiStringCStrOffset,
                              // kVtableCAurGUIStringInternal, CExoString
#include "engine_player.h"    // kAddrAppManagerPtr,
                              // kAppManagerClientAppOffset,
                              // kClientExoAppInternalOffset
#include "log.h"

namespace {

// Resolve chain — same offsets engine_picker uses. Re-stated locally to
// avoid pulling that module's namespace in for a four-line walk.
constexpr size_t kInternalGuiInGameOffset      = 0x040;
constexpr size_t kGuiInGameMainInterfaceOffset = 0x90;

// CSWGuiTargetActionMenu lives inside CSWGuiMainInterface at +0xBC. Verified
// against the C header (swkotor.exe.h:11611 MEMBER OFFSET="0xbc" SIZE="0x1af0").
constexpr size_t kMainInterfaceTargetActionMenuOffset = 0xBC;

// CExoArrayList<CSWGuiInterfaceAction> action_lists[3] occupy TAM +0x00
// through TAM +0x24 (3 * 0x0C). We only need the size field, which lives
// at +0x04 within each CExoArrayList.
constexpr size_t kTamActionListsOffset    = 0x00;
constexpr size_t kActionListSizeOffset    = 0x04;
constexpr size_t kActionListStride        = 0x0C;

// CSWGuiMainInterfaceAction stride / row offset within TAM. target_actions[3]
// starts at +0x54 (after action_lists[3] @+0x00 + field1[12] @+0x24).
constexpr size_t kTamTargetActionsOffset  = 0x54;
constexpr size_t kTargetActionStride      = 0x71C;

// Within a CSWGuiMainInterfaceAction, action_button is the first member at
// offset 0. is_action lives at +0x718 (one int before the next stride
// boundary at 0x71C). Embedded buttons: action_label at +0x1C4
// (CSWGuiButton size 0x1C4), up_button +0x388, down_button +0x54C, then
// field4 +0x710, field5 +0x714, is_action +0x718.
constexpr size_t kRowActionButtonOffset   = 0x000;
constexpr size_t kRowActionLabelOffset    = 0x1C4;
constexpr size_t kRowUpButtonOffset       = 0x388;
constexpr size_t kRowDownButtonOffset     = 0x54C;
constexpr size_t kRowField4Offset         = 0x710;
constexpr size_t kRowField5Offset         = 0x714;
constexpr size_t kRowIsActionOffset       = 0x718;

// CSWGuiInterfaceAction stride/offsets — same as engine_picker uses on
// the descriptor read. Re-stated here so the wide-diagnostic peek into
// action_lists[r].data[0] doesn't depend on the picker module.
constexpr size_t kIfActionLabelOffset    = 0x00;  // CExoString
constexpr size_t kIfActionIdOffset       = 0x08;  // ulong
constexpr size_t kIfActionTargetOffset   = 0x1c;  // ulong
constexpr size_t kIfActionIconOffset     = 0x20;  // CResRef (16B)
constexpr size_t kResRefMaxLen           = 16;

// CSWGuiLabel name_label sits at TAM +0x15CC (relative). Reading via
// kLabelGuiStringPtrOffset (0xE4) for the rendered c_string.
constexpr size_t kTamNameLabelOffset      = 0x15CC;

// Engine entry points (k1_win_gog_swkotor.exe.xml symbol table; GoG bytes
// match Steam per project_ghidra_gog_steam_bytes_match).
constexpr uintptr_t kAddrSelectNextAction = 0x006865b0;
constexpr uintptr_t kAddrSelectPrevAction = 0x00686680;
constexpr uintptr_t kAddrDoTargetAction   = 0x00689610;

typedef void (__thiscall* PFN_RowOp)(void* this_, int row);

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

// Local copy of the gui_string read path used by ExtractAnnounceableText —
// pulled in here so the engine layer doesn't depend on menus.cpp's helpers.
// See engine_offsets.h for why we validate the vtable before dereffing
// gui_string.
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

// Inline CExoString fallback. Same shape as engine_picker's ReadExoString
// but copy-pasted to avoid the cross-module include.
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

void* RowActionAddr(void* tam, int row) {
    if (!tam || row < 0 || row >= acc::engine_radial::kRowCount) return nullptr;
    return reinterpret_cast<unsigned char*>(tam) +
           kTamTargetActionsOffset + row * kTargetActionStride;
}

void* RowActionButtonAddr(void* tam, int row) {
    void* rowBase = RowActionAddr(tam, row);
    if (!rowBase) return nullptr;
    return reinterpret_cast<unsigned char*>(rowBase) + kRowActionButtonOffset;
}

}  // namespace

namespace acc::engine_radial {

void* ResolveTargetActionMenu() {
    void* exoApp   = GetClientExoApp();
    void* internal = GetClientExoAppInternal(exoApp);
    void* guiIn    = GetGuiInGame(internal);
    void* mainIf   = GetMainInterface(guiIn);
    if (!mainIf) return nullptr;
    return reinterpret_cast<unsigned char*>(mainIf) +
           kMainInterfaceTargetActionMenuOffset;
}

int RowActionCount(void* tam, int row) {
    if (!tam || row < 0 || row >= kRowCount) return 0;
    int32_t size = 0;
    size_t offset = kTamActionListsOffset + row * kActionListStride +
                    kActionListSizeOffset;
    if (!ReadInt32(tam, offset, &size)) return 0;
    if (size < 0) return 0;
    return static_cast<int>(size);
}

bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    void* btn = RowActionButtonAddr(tam, row);
    if (!btn) return false;

    // gui_string is the rendered text — same priority order menus.cpp's
    // ExtractAnnounceableText uses for any other CSWGuiButton.
    if (ReadGuiStringLocal(btn, kButtonGuiStringPtrOffset, outBuf, bufSize)) {
        return true;
    }
    // Fall back to the inline CExoString. The engine sometimes leaves
    // gui_string null transiently between SelectNextAction calls.
    if (ReadCExoStringLocal(btn, kButtonTextOffset, outBuf, bufSize)) {
        return true;
    }
    // strref / TLK fallback intentionally omitted in this first cut. The
    // engine populates target_actions[row].action_button via SetText with
    // a localised CExoString from the action descriptor, so the strref
    // path is rarely the source of truth here. Adding it is a follow-up
    // if logs show the inline CExoString empty in real usage.
    return false;
}

bool ReadTargetName(void* tam, char* outBuf, size_t bufSize) {
    if (!tam || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    void* nameLabel = reinterpret_cast<unsigned char*>(tam) + kTamNameLabelOffset;
    if (ReadGuiStringLocal(nameLabel, kLabelGuiStringPtrOffset, outBuf, bufSize)) {
        return true;
    }
    if (ReadCExoStringLocal(nameLabel, kLabelTextOffset, outBuf, bufSize)) {
        return true;
    }
    return false;
}

void LogState(void* tam, const char* tag) {
    if (!tam) {
        acclog::Write("Radial.State[%s]: tam=NULL", tag ? tag : "?");
        return;
    }

    // Hex dump the first 0x40 bytes of TAM (covers all 3 action_lists +
    // half of field1[12]). Eight uint32 per line.
    char hex[160];
    for (int line = 0; line < 4; ++line) {
        size_t off = line * 0x10;
        uint32_t w[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            int32_t tmp = 0;
            ReadInt32(tam, off + i * 4, &tmp);
            w[i] = static_cast<uint32_t>(tmp);
        }
        std::snprintf(hex, sizeof(hex),
                      "Radial.State[%s]: tam+%02x: %08x %08x %08x %08x",
                      tag ? tag : "?", static_cast<unsigned>(off),
                      w[0], w[1], w[2], w[3]);
        acclog::Write("%s", hex);
    }

    // Parsed action_lists.
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        size_t base = kTamActionListsOffset + r * kActionListStride;
        int32_t dataPtr = 0, size = 0, cap = 0;
        ReadInt32(tam, base + 0x00, &dataPtr);
        ReadInt32(tam, base + 0x04, &size);
        ReadInt32(tam, base + 0x08, &cap);
        acclog::Write(
            "Radial.State[%s]: action_lists[%d] data=0x%08x size=%d cap=%d",
            tag ? tag : "?", r,
            static_cast<uint32_t>(dataPtr), size, cap);
    }

    // Per-row is_action + action_button.text length (via gui_string).
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        size_t rowBase = kTamTargetActionsOffset + r * kTargetActionStride;
        int32_t isAction = 0;
        ReadInt32(tam, rowBase + kRowIsActionOffset, &isAction);

        char label[128] = "";
        void* btn = reinterpret_cast<unsigned char*>(tam) + rowBase +
                    kRowActionButtonOffset;
        ReadGuiStringLocal(btn, kButtonGuiStringPtrOffset,
                           label, sizeof(label));
        size_t labelLen = 0;
        for (; labelLen < sizeof(label) && label[labelLen]; ++labelLen) {}

        acclog::Write(
            "Radial.State[%s]: target_actions[%d] is_action=%d "
            "action_button.gui_string=[%s] (len=%zu)",
            tag ? tag : "?", r, isAction, label, labelLen);
    }

    char nameBuf[64] = "";
    acc::engine_radial::ReadTargetName(tam, nameBuf, sizeof(nameBuf));
    acclog::Write("Radial.State[%s]: name_label.text=[%s]",
                  tag ? tag : "?", nameBuf);
}

namespace {

// Read a CResRef (fixed 16-byte string) into outBuf, treating the buffer
// as NUL-terminated within the 16-byte window.
void ReadResRefLocal(void* base, size_t offset,
                     char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return;
    outBuf[0] = '\0';
    if (!base) return;
    __try {
        const char* src = reinterpret_cast<const char*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        size_t lim = bufSize - 1;
        if (lim > kResRefMaxLen) lim = kResRefMaxLen;
        size_t i = 0;
        for (; i < lim; ++i) {
            char c = src[i];
            outBuf[i] = c;
            if (c == '\0') return;
        }
        outBuf[i] = '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
}

// Read a CSWGuiButton's text via every path we know about (gui_string ->
// inline CExoString) into outBuf. Always NUL-terminates; safe on a stale
// embedded button.
void ReadButtonText(void* button, char* outBuf, size_t bufSize) {
    if (!button || !outBuf || bufSize == 0) return;
    outBuf[0] = '\0';
    if (ReadGuiStringLocal(button, kButtonGuiStringPtrOffset,
                           outBuf, bufSize)) return;
    if (ReadCExoStringLocal(button, kButtonTextOffset, outBuf, bufSize)) return;
}

}  // namespace

void LogStateWide(void* tam, const char* tag) {
    // Re-use the standard dump as the prefix so log readers don't have to
    // chase two separate snapshot blocks for the same call.
    LogState(tam, tag);
    if (!tam) return;

    // field1[12] — 12 ints starting at TAM +0x24. Hypothesis: per-row
    // selected-action index per mode, with -1 meaning "no selection".
    {
        int32_t f[12] = {0};
        for (int i = 0; i < 12; ++i) {
            ReadInt32(tam, 0x24 + i * 4, &f[i]);
        }
        acclog::Write(
            "Radial.StateW[%s]: field1[0..3]=%d,%d,%d,%d "
            "[4..7]=%d,%d,%d,%d [8..11]=%d,%d,%d,%d",
            tag ? tag : "?",
            f[0], f[1], f[2], f[3],
            f[4], f[5], f[6], f[7],
            f[8], f[9], f[10], f[11]);
    }

    // For each row: full button text dump + field4/field5 + is_action.
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        size_t rowBase = kTamTargetActionsOffset + r * kTargetActionStride;
        unsigned char* row =
            reinterpret_cast<unsigned char*>(tam) + rowBase;
        char actBtn[96]   = "";
        char actLbl[96]   = "";
        char upBtn[96]    = "";
        char downBtn[96]  = "";
        ReadButtonText(row + kRowActionButtonOffset, actBtn, sizeof(actBtn));
        ReadButtonText(row + kRowActionLabelOffset,  actLbl, sizeof(actLbl));
        ReadButtonText(row + kRowUpButtonOffset,     upBtn,  sizeof(upBtn));
        ReadButtonText(row + kRowDownButtonOffset,   downBtn, sizeof(downBtn));
        int32_t f4 = 0, f5 = 0, isAct = 0;
        ReadInt32(tam, rowBase + kRowField4Offset, &f4);
        ReadInt32(tam, rowBase + kRowField5Offset, &f5);
        ReadInt32(tam, rowBase + kRowIsActionOffset, &isAct);

        acclog::Write(
            "Radial.StateW[%s]: target_actions[%d] is_action=%d "
            "field4=0x%08x field5=0x%08x",
            tag ? tag : "?", r, isAct,
            static_cast<uint32_t>(f4), static_cast<uint32_t>(f5));
        acclog::Write(
            "Radial.StateW[%s]: target_actions[%d] action_button=[%s] "
            "action_label=[%s]",
            tag ? tag : "?", r, actBtn, actLbl);
        acclog::Write(
            "Radial.StateW[%s]: target_actions[%d] up_button=[%s] "
            "down_button=[%s]",
            tag ? tag : "?", r, upBtn, downBtn);
    }

    // Peek action_lists[r].data[0] when the engine reserved capacity but
    // didn't update size — maybe the action descriptor was written but
    // the size update was skipped, leaving us a recoverable label.
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        size_t base = kTamActionListsOffset + r * kActionListStride;
        int32_t dataPtr = 0, sizeF = 0, cap = 0;
        ReadInt32(tam, base + 0x00, &dataPtr);
        ReadInt32(tam, base + 0x04, &sizeF);
        ReadInt32(tam, base + 0x08, &cap);
        if (cap <= 0 || dataPtr == 0) continue;
        void* peek = reinterpret_cast<void*>(static_cast<uintptr_t>(dataPtr));

        char    label[64] = "";
        char    icon[16]  = "";
        int32_t actionId = 0;
        int32_t targetId = 0;
        ReadCExoStringLocal(peek, kIfActionLabelOffset, label, sizeof(label));
        ReadInt32(peek, kIfActionIdOffset,    &actionId);
        ReadInt32(peek, kIfActionTargetOffset, &targetId);
        ReadResRefLocal(peek, kIfActionIconOffset, icon, sizeof(icon));

        acclog::Write(
            "Radial.StateW[%s]: action_lists[%d].data[0] peek "
            "label=[%s] action_id=0x%x target_id=0x%08x icon=[%s] "
            "(size=%d cap=%d data=0x%08x)",
            tag ? tag : "?", r, label,
            static_cast<uint32_t>(actionId),
            static_cast<uint32_t>(targetId), icon,
            sizeF, cap, static_cast<uint32_t>(dataPtr));
    }
}

bool SelectNextActionInRow(void* tam, int row) {
    if (!tam || row < 0 || row >= kRowCount) return false;
    __try {
        auto fn = reinterpret_cast<PFN_RowOp>(kAddrSelectNextAction);
        fn(tam, row);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Radial: SelectNextActionInRow SEH-FAULT row=%d", row);
        return false;
    }
}

bool SelectPrevActionInRow(void* tam, int row) {
    if (!tam || row < 0 || row >= kRowCount) return false;
    __try {
        auto fn = reinterpret_cast<PFN_RowOp>(kAddrSelectPrevAction);
        fn(tam, row);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Radial: SelectPrevActionInRow SEH-FAULT row=%d", row);
        return false;
    }
}

bool DispatchRowAction(void* tam, int row) {
    if (!tam || row < 0 || row >= kRowCount) return false;
    __try {
        auto fn = reinterpret_cast<PFN_RowOp>(kAddrDoTargetAction);
        fn(tam, row);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Radial: DispatchRowAction SEH-FAULT row=%d", row);
        return false;
    }
}

}  // namespace acc::engine_radial
