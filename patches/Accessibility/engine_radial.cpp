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
                              // kClientExoAppInternalOffset,
                              // kClientObjectServerObjectOffset,
                              // GetClientLeader (for Security skill check)
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

// field1[12] = 4 target_types × 3 rows; each int is the selected action_id
// for that combination (-1 = use first item). Used by inner-PopulateMenus
// + SelectNext/Prev/DoTargetAction to mark the active action in
// action_lists[row]. Source-of-truth for "which descriptor is selected
// right now"; the rendered target_actions[r].action_button only catches
// up after the engine's next paint pass, which is why we sometimes see
// `gui_string=[] action_button=[]` immediately after PopulateMenus even
// though the data is fully populated.
constexpr size_t kTamField1Offset         = 0x24;
constexpr size_t kTamTargetTypeOffset     = 0x1AEA;
constexpr int    kActionIdNone            = -1;
constexpr size_t kIfActionStride          = 0x38;

// Engine entry points (k1_win_gog_swkotor.exe.xml symbol table; GoG bytes
// match Steam per project_ghidra_gog_steam_bytes_match).
constexpr uintptr_t kAddrSelectNextAction = 0x006865b0;
constexpr uintptr_t kAddrSelectPrevAction = 0x00686680;
constexpr uintptr_t kAddrDoTargetAction   = 0x00689610;

// CClientExoApp::GetGameObject @ 0x005ED580 — same address engine_area /
// engine_picker use. (this, handle) -> CGameObject*.
constexpr uintptr_t kAddrCClientGetGameObject = 0x005ED580;

// CSWCCreatureStats::GetCanUseSkill @ 0x006477e0 — bool __thiscall
// (CSWCCreatureStats*, ushort skill_idx). Used by CSWCDoor::GetTargetActions
// to gate the Security row (skill_idx=6 = Security).
constexpr uintptr_t kAddrGetCanUseSkill = 0x006477E0;

// CSWCCreature.lvl_up_stats @ +0x2f8 → CSWCLevelUpStats*. The CSWC-
// CreatureStats inline-member sits at +0x00 of CSWCLevelUpStats, so this
// pointer can be reinterpreted directly as CSWCCreatureStats*. Verified:
// swkotor.exe.h:5984 (lvl_up_stats), :6332 (creature_stats inline at +0).
constexpr size_t kCreatureLvlUpStatsOffset = 0x2F8;

// CSWSDoor.field_0x2d8 — the second Security gate from CSWCDoor::Get-
// TargetActions decomp: must be 0 for the Security row to populate.
// Per the door decomp, the field is read as `*(int *)(server_door + 0x2d8)`
// and compared `== 0`.
constexpr size_t kServerDoorSecurityGateOffset = 0x2D8;

// Skill index for "Security" — door decomp passes literal `6` to
// GetCanUseSkill. Re-stated as a constant for log readability.
constexpr int kSkillSecurity = 6;

// GameObjectMethods vtable indices we care about for runtime-class probing.
// Layout from swkotor.exe.h:14638. Each entry is one __thiscall pointer
// (4 bytes on x86); offsets are byte offsets into the vtable.
//   [0] _destructor
//   [1] SetId
//   [2] ResetUpdateTimes
//   [3] AsSWCObject     (+0x0C)
//   [4] AsSWSObject     (+0x10)
//   [5] AsSWCDoor       (+0x14)
//   [6] AsSWSDoor       (+0x18)
//   [10] AsSWCCreature  (+0x28)
//   [14] AsSWCTrigger   (+0x38)
//   [18] AsSWCPlaceable (+0x48)
constexpr size_t kVtableAsSWCDoorOffset      = 0x14;
constexpr size_t kVtableAsSWCCreatureOffset  = 0x28;
constexpr size_t kVtableAsSWCTriggerOffset   = 0x38;
constexpr size_t kVtableAsSWCPlaceableOffset = 0x48;

// CSWCDoor field offsets (swkotor.exe.h:6192 STRUCTURE NAME="CSWCDoor"):
//   +0x104  cannot_bash       (int) — gates Bash row
//   +0x108  can_use_actions   (int) — gates BOTH Bash and Security rows
//                                     (per CSWCDoor::GetTargetActions decomp)
//   +0x114  is_hostile        (int)
//   +0x11c  state             (int) — door open/closed state
//   +0x138  field17           (int) — additional Bash-row gate
constexpr size_t kDoorCannotBashOffset     = 0x104;
constexpr size_t kDoorCanUseActionsOffset  = 0x108;
constexpr size_t kDoorIsHostileOffset      = 0x114;
constexpr size_t kDoorStateOffset          = 0x11c;
constexpr size_t kDoorField17Offset        = 0x138;

typedef void (__thiscall* PFN_RowOp)(void* this_, int row);
typedef void* (__thiscall* PFN_GetGameObject)(void* exoApp, uint32_t handle);
typedef void* (__thiscall* PFN_AsClass)(void* gameObject);
typedef bool (__thiscall* PFN_GetCanUseSkill)(void* stats, uint16_t skillIdx);

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

namespace {

// Find the CSWGuiInterfaceAction* in action_lists[row].data that matches
// the engine's currently-selected action_id (field1[target_type*3 + row]).
// Returns data[0] when field1 holds -1 ("no selection, use default") or
// when no entry matches the stored id (engine fallback). Returns nullptr
// when the row is empty / the data pointer is null / a fault occurs.
void* FindSelectedActionDescriptor(void* tam, int row) {
    if (!tam || row < 0 || row >= acc::engine_radial::kRowCount) return nullptr;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(tam);
        size_t listOff = kTamActionListsOffset + row * kActionListStride;
        void* dataPtr = *reinterpret_cast<void**>(base + listOff);
        int32_t size  = *reinterpret_cast<int32_t*>(base + listOff + kActionListSizeOffset);
        if (!dataPtr || size <= 0) return nullptr;

        uint8_t targetType = *(base + kTamTargetTypeOffset);
        int32_t selectedId = kActionIdNone;
        if (targetType < 4) {
            selectedId = *reinterpret_cast<int32_t*>(
                base + kTamField1Offset +
                (static_cast<size_t>(targetType) * 3 + row) * sizeof(int32_t));
        }

        // -1 = "no selection, default to first"; otherwise scan for match
        // and fall through to data[0] when no entry has the stored id
        // (mirrors what the engine does in SelectNextAction's lookup loop).
        if (selectedId != kActionIdNone) {
            auto* entries = reinterpret_cast<unsigned char*>(dataPtr);
            for (int i = 0; i < size; ++i) {
                int32_t id = *reinterpret_cast<int32_t*>(
                    entries + i * kIfActionStride + kIfActionIdOffset);
                if (id == selectedId) {
                    return entries + i * kIfActionStride;
                }
            }
        }
        return dataPtr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

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
    // Source-of-truth fallback: read the label directly from
    // action_lists[row].data[selected]. This is the descriptor the engine
    // *will* render into action_button on its next paint pass — at populate
    // time the rendered button text is empty (verified in
    // patch-20260505-101621.log: action_lists[1].data[0] peek showed
    // [Sicherheit] while target_actions[1].action_button was []). Without
    // this fallback the user hears "Aktion 1/1" with no label on the very
    // first arm of a freshly-populated row.
    void* descriptor = FindSelectedActionDescriptor(tam, row);
    if (descriptor &&
        ReadCExoStringLocal(descriptor, kIfActionLabelOffset,
                            outBuf, bufSize)) {
        return true;
    }
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
        acclog::Write("Radial.State", "[%s] tam=NULL", tag ? tag : "?");
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
                      "[%s] tam+%02x: %08x %08x %08x %08x",
                      tag ? tag : "?", static_cast<unsigned>(off),
                      w[0], w[1], w[2], w[3]);
        acclog::Write("Radial.State", "%s", hex);
    }

    // Parsed action_lists.
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        size_t base = kTamActionListsOffset + r * kActionListStride;
        int32_t dataPtr = 0, size = 0, cap = 0;
        ReadInt32(tam, base + 0x00, &dataPtr);
        ReadInt32(tam, base + 0x04, &size);
        ReadInt32(tam, base + 0x08, &cap);
        acclog::Write("Radial.State", "[%s] action_lists[%d] data=0x%08x size=%d cap=%d",
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

        acclog::Write("Radial.State", "[%s] target_actions[%d] is_action=%d "
            "action_button.gui_string=[%s] (len=%zu)",
            tag ? tag : "?", r, isAction, label, labelLen);
    }

    char nameBuf[64] = "";
    acc::engine_radial::ReadTargetName(tam, nameBuf, sizeof(nameBuf));
    acclog::Write("Radial.State", "[%s] name_label.text=[%s]",
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
        acclog::Write("Radial.StateW", "[%s] field1[0..3]=%d,%d,%d,%d "
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

        acclog::Write("Radial.StateW", "[%s] target_actions[%d] is_action=%d "
            "field4=0x%08x field5=0x%08x",
            tag ? tag : "?", r, isAct,
            static_cast<uint32_t>(f4), static_cast<uint32_t>(f5));
        acclog::Write("Radial.StateW", "[%s] target_actions[%d] action_button=[%s] "
            "action_label=[%s]",
            tag ? tag : "?", r, actBtn, actLbl);
        acclog::Write("Radial.StateW", "[%s] target_actions[%d] up_button=[%s] "
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

        acclog::Write("Radial.StateW", "[%s] action_lists[%d].data[0] peek "
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
        acclog::Write("Radial", "SelectNextActionInRow SEH-FAULT row=%d", row);
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
        acclog::Write("Radial", "SelectPrevActionInRow SEH-FAULT row=%d", row);
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
        acclog::Write("Radial", "DispatchRowAction SEH-FAULT row=%d", row);
        return false;
    }
}

void* GetRowActionButton(void* tam, int row) {
    if (!tam || row < 0 || row >= kRowCount) return nullptr;
    // target_actions[row].action_button. action_button is at offset 0
    // within CSWGuiMainInterfaceAction so its address equals the
    // array-entry address.
    return reinterpret_cast<unsigned char*>(tam) +
           kTamTargetActionsOffset +
           static_cast<size_t>(row) * kTargetActionStride +
           kRowActionButtonOffset;
}

namespace {

// Call vtable->AsSWCxxx(gameObject) at the given vtable byte-offset.
// Returns nullptr on null input or any fault.
void* CallVtableAsClass(void* gameObject, size_t vtableOffset) {
    if (!gameObject) return nullptr;
    __try {
        unsigned char** vtablePtr =
            *reinterpret_cast<unsigned char***>(gameObject);
        if (!vtablePtr) return nullptr;
        unsigned char* slot =
            *reinterpret_cast<unsigned char**>(
                reinterpret_cast<unsigned char*>(vtablePtr) + vtableOffset);
        if (!slot) return nullptr;
        auto fn = reinterpret_cast<PFN_AsClass>(slot);
        return fn(gameObject);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

void LogTargetDiag(uint32_t targetClient, const char* tag) {
    const char* t = tag ? tag : "?";

    if (targetClient == 0u || targetClient == 0xFFFFFFFFu ||
        targetClient == 0x7F000000u) {
        acclog::Write("Radial.Diag", "[%s] target=0x%08x — sentinel/invalid",
                      t, targetClient);
        return;
    }

    void* exoApp = GetClientExoApp();
    if (!exoApp) {
        acclog::Write("Radial.Diag", "[%s] target=0x%08x — no client exo app",
                      t, targetClient);
        return;
    }

    void* gameObject = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_GetGameObject>(
            kAddrCClientGetGameObject);
        gameObject = fn(exoApp, targetClient);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gameObject = nullptr;
    }
    if (!gameObject) {
        acclog::Write("Radial.Diag", "[%s] target=0x%08x — GetGameObject returned NULL",
            t, targetClient);
        return;
    }

    // Read the vtable address itself for log identification — different
    // runtime classes have different vtables, so this is a quick "is the
    // door we got the same one between calls" check.
    uintptr_t vtableAddr = 0;
    __try {
        vtableAddr = *reinterpret_cast<uintptr_t*>(gameObject);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        vtableAddr = 0;
    }

    void* asDoor      = CallVtableAsClass(gameObject, kVtableAsSWCDoorOffset);
    void* asCreature  = CallVtableAsClass(gameObject, kVtableAsSWCCreatureOffset);
    void* asPlaceable = CallVtableAsClass(gameObject, kVtableAsSWCPlaceableOffset);
    void* asTrigger   = CallVtableAsClass(gameObject, kVtableAsSWCTriggerOffset);

    const char* kind = "unknown";
    if      (asDoor)      kind = "door";
    else if (asCreature)  kind = "creature";
    else if (asPlaceable) kind = "placeable";
    else if (asTrigger)   kind = "trigger";

    acclog::Write("Radial.Diag", "[%s] target=0x%08x gameObject=%p vtable=0x%08x kind=%s "
        "(door=%p creature=%p placeable=%p trigger=%p)",
        t, targetClient, gameObject,
        static_cast<uint32_t>(vtableAddr), kind,
        asDoor, asCreature, asPlaceable, asTrigger);

    if (asDoor) {
        int32_t cannotBash = -1;
        int32_t canUseActions = -1;
        int32_t isHostile = -1;
        int32_t state = -1;
        int32_t field17 = -1;
        ReadInt32(asDoor, kDoorCannotBashOffset,    &cannotBash);
        ReadInt32(asDoor, kDoorCanUseActionsOffset, &canUseActions);
        ReadInt32(asDoor, kDoorIsHostileOffset,     &isHostile);
        ReadInt32(asDoor, kDoorStateOffset,         &state);
        ReadInt32(asDoor, kDoorField17Offset,       &field17);
        acclog::Write("Radial.Diag", "[%s] door=%p cannot_bash=%d can_use_actions=%d "
            "is_hostile=%d state=%d field17=%d",
            t, asDoor, cannotBash, canUseActions, isHostile, state, field17);

        // Server-side door's Security gate — `*(int *)(server_door + 0x2d8)`
        // must be 0 for CSWCDoor::GetTargetActions to add the Security row.
        // Reach via the same +0xf8 chain we use for player creature; the
        // SWS object lives at the same offset for every client object.
        void* serverDoor = nullptr;
        __try {
            serverDoor = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(asDoor) +
                kClientObjectServerObjectOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            serverDoor = nullptr;
        }
        int32_t securityGate = -1;
        bool gateRead = false;
        if (serverDoor) {
            gateRead = ReadInt32(serverDoor,
                                 kServerDoorSecurityGateOffset, &securityGate);
        }
        if (gateRead) {
            acclog::Write("Radial.Diag", "[%s] server_door=%p +0x2d8(security_gate)=%d "
                "(must be 0 to add Security row)",
                t, serverDoor, securityGate);
        } else {
            acclog::Write("Radial.Diag", "[%s] server_door=%p — security_gate read failed",
                t, serverDoor);
        }

        // Active leader's Security skill check — same call CSWCDoor::Get-
        // TargetActions makes: GetCanUseSkill(&leader->lvl_up_stats->
        // creature_stats, 6). lvl_up_stats is a pointer at leader+0x2f8;
        // CSWCCreatureStats is the inline first member of CSWCLevelUpStats
        // so the pointer can be passed straight to GetCanUseSkill.
        void* clientLeader = acc::engine::GetClientLeader();
        int leaderSecurity = -1;
        if (clientLeader) {
            void* lvlUpStats = nullptr;
            __try {
                lvlUpStats = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(clientLeader) +
                    kCreatureLvlUpStatsOffset);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                lvlUpStats = nullptr;
            }
            if (lvlUpStats) {
                __try {
                    auto fn = reinterpret_cast<PFN_GetCanUseSkill>(
                        kAddrGetCanUseSkill);
                    leaderSecurity = fn(lvlUpStats,
                                        static_cast<uint16_t>(kSkillSecurity))
                                     ? 1 : 0;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    leaderSecurity = -1;
                }
            }
        }
        acclog::Write("Radial.Diag", "[%s] leader=%p has_security_skill=%d "
            "(skill 6, gates the Security row in CSWCDoor::GetTargetActions)",
            t, clientLeader, leaderSecurity);

        // Annotate which precondition would fail — saves cross-referencing
        // the GetTargetActions decomp every time a log lands.
        if (canUseActions == 0) {
            acclog::Write("Radial.Diag", "[%s] door precondition fail — can_use_actions=0 "
                "(door already in final state, e.g. opened); both Bash and "
                "Security rows skipped by CSWCDoor::GetTargetActions",
                t);
        }
        if (cannotBash != 0) {
            acclog::Write("Radial.Diag", "[%s] door precondition fail — cannot_bash=%d "
                "(scripted door, no Bash row even when can_use_actions != 0)",
                t, cannotBash);
        }
        if (field17 != 0) {
            acclog::Write("Radial.Diag", "[%s] door precondition fail — field17=%d "
                "(blocks Bash row regardless of cannot_bash / can_use_actions)",
                t, field17);
        }
        if (gateRead && securityGate != 0) {
            acclog::Write("Radial.Diag", "[%s] door precondition fail — server_door+0x2d8 "
                "= %d != 0 (door rejects Security row at the server-side gate)",
                t, securityGate);
        }
        if (canUseActions != 0 && leaderSecurity == 0) {
            acclog::Write("Radial.Diag", "[%s] door precondition fail — active leader "
                "lacks Security skill (skill 6); Security row skipped even "
                "when door's preconditions allow it",
                t);
        }
    }
}

}  // namespace acc::engine_radial
