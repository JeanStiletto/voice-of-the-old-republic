#include "engine_actionbar.h"

#include <windows.h>

#include "engine_offsets.h"
#include "engine_app.h"      // GetClientApp, GetClientAppInternal
#include "engine_area.h"     // ResolveClientObject — LogDispatchDiag's probe
#include "engine_game.h"     // IsKotor2 — medpick probe is K2-only
#include "engine_panels.h"   // ResolveGuiInGame, ResolveMainInterface
#include "log.h"
#include "engine_rebase.h"
#include "engine_offsets_select.h"

namespace {

// CSWGuiMainInterface field7_0x1bac..field12_0x1bc0 — six int32s, one
// per column, holding the "currently-selected variant action_id" for
// that column. DoPersonalAction reads `*(this + 0x1bac + slot*4)` and
// searches the column's list for the matching action_id (falls back
// to data[0] on no-match). SelectPrevPersonalAction writes here when
// the user cycles via mouse-wheel / arrow button.
// K2 witnessed in the K2 DoPersonalAction (0x00751750): selected-id array read
// as [this+0x1c7c + slot*4], column index stored at +0x1c98, personal lists at
// [this+0x78 + slot*0xc] (data +0 / size +4). NOTE: K2 iterates SEVEN columns
// (param < 7) where KOTOR 1 has six — kColumnCount stays 6 for our surface,
// the seventh is additive at the end and unread by us.
const size_t kSelectedActionIdArrayOffset = acc::off::Pick(0x1bac, 0x1c7c);

// CSWGuiMainInterface.field5_0x74[6] — six CExoArrayList<CSWGuiInterfaceAction>.
// Verified populated 2026-05-05: slot 1 reported size=2 matching the two
// medikits in inventory.
const size_t kPersonalListsOffset    = acc::off::Pick(0x74, 0x78);
const size_t kPersonalListStride     = acc::off::Same(0x0C);
const size_t kPersonalListDataOffset = acc::off::Same(0x00);  // T** data
const size_t kPersonalListSizeOffset = acc::off::Same(0x04);  // int size

// CSWGuiInterfaceAction layout — same as engine_radial / engine_picker.
// K2 stride witnessed 0x3C in BOTH K2 Do*Action bodies (`idx * 0x3c`) — one
// dword grew somewhere past +0x30; id (+8) and the +0x30 flag word are
// unchanged, so the growth is at the tail and does not move our fields.
const size_t kIfActionLabelOffset    = acc::off::Same(0x00);  // CExoString (+0..+7, id at +8 pins it)
const size_t kIfActionIdOffset       = acc::off::Same(0x08);  // ulong
const size_t kIfActionStride         = acc::off::Pick(0x38, 0x3c);

// The two fields DoPersonalAction itself tests before it will dispatch. Both
// games read them at the SAME offsets (K1 decompile of 0x0068ad60; K2 listing
// at 0x007518ca — `mov eax,[edx+0x30] / and eax,1 / je <refusal>`, then the
// null test on the handler at +0x0c).
//
//   +0x30  flag word. Bit 0 = "this entry can be used right now". Bits 1..4
//          are a reason code, 1..6, mapping to the six StrRefs below.
//   +0x0c  the entry's handler (a pointer-to-member; the first dword is
//          enough — the engine's own null test reads exactly this dword).
const size_t kIfActionFlagsOffset    = acc::off::Same(0x30);
const size_t kIfActionHandlerOffset  = acc::off::Same(0x0c);

// The entry's baked creature reference — what the dispatch resolves (and on
// K2 bails on in total silence when the resolve fails). Same +0x1c in the K1
// decompile of DoPersonalAction and the K2 twin (its appender writes the
// owning creature's own id here for every personal item entry).
const size_t kIfActionCreatureOffset = acc::off::Same(0x1c);

// The two K2 item-entry handlers, byte-witnessed in the K2 appender
// (0x0077BCF0): the medical two-step arm/consume flow, and the plain
// one-step inventory use. KOTOR-2-only values; every consumer gates on
// IsKotor2 before comparing.
const int32_t kK2MedicalUseHandler   = 0x0077C780;
const int32_t kK2InventoryUseHandler = 0x0077CC80;

// The engine's own refusal strings, in reason-code order (code 1 → [0]).
// Byte-identical ids in both binaries — it is what identified the K2 twin of
// DoPersonalAction in the first place.
constexpr uint32_t kRefusalStrRefs[6] = {
    0x96d5, 0x96d6, 0x96d7, 0xa5b6, 0xa602, 0xbb40,
};

// Engine entry points (verified from k1_win_gog_swkotor.exe.xml +
// docs/action-menu-investigation.md). GoG bytes match Steam per memory
// project_ghidra_gog_steam_bytes_match.
// K2 twin 0x00751750 found by its unique six-StrRef "can't do that" switch
// (0x96d5..0xbb40, identical ids) + personal-list/selected-array reads.
// Byte-verified ret 8 on BOTH games — the two-arg typedef stays correct.
const uintptr_t kAddrDoPersonalAction = acc::addr::Pick(0x0068ad60, 0x00751750);

// CGuiInGame::SetMainInterfaceTarget @ 0x0062b000 — same wrapper as
// the radial/picker drive uses. Thin forwarder to
// CSWGuiMainInterface::SetTarget (stores field1_0x64 + resets the
// refresh-hint float field21_0x5cb0). K2 twin byte-verified: guards
// CGuiInGame+0x98 (the Batch-2 MainInterface slot), forwards both dwords
// to CSWGuiMainInterface::SetTarget 0x0074CFB0 (target +0x68, hint
// +0x595c), ret 8 — the pad-dword typedef stays correct.
const uintptr_t kAddrSetMainInterfaceTarget = acc::addr::Pick(0x0062b000, 0x007CE710);

// CGuiInGame::RePopulateMainInterface @ 0x0062b050 — thin forwarder to
// CSWGuiMainInterface::PopulateMenus @ 0x00689d80. Refreshes both the
// six personal-action lists (field5_0x74[0..5] via GetPersonalActions)
// and target_action_menu.action_lists[0..2] (via GetTargetActions for
// each row) against the currently-stamped main-interface target.
// K2 twin 0x007CEB50 (39 bytes, ret 0): same +0x98 guard, calls the K2
// PopulateMenus 0x0074CFE0 passing its NEW int arg = 1 internally — so
// this argless forwarder is the safest K2 entry for a full repopulate.
const uintptr_t kAddrRePopulateMainInterface = acc::addr::Pick(0x0062b050, 0x007CEB50);

typedef void (__thiscall* PFN_DoPersonalAction)(void* this_,
                                                int slot, int param_2);
// ret 8: the callee purges TWO dwords (Ghidra BYTES_PURGED="8") though it only
// uses param_1. A single-arg typedef under-pushes by 4 and corrupts the caller's
// frame — the SetMainInterfaceTarget stack bug (see engine_picker.cpp). Push a
// matching unused second dword to balance the cleanup.
typedef void (__thiscall* PFN_SetMainInterfaceTarget)(void* this_,
                                                      uint32_t target,
                                                      uint32_t pad);
typedef void (__thiscall* PFN_RePopulateMainInterface)(void* this_);

// Local chain helpers (same shape as engine_radial / engine_picker).
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

// Kept as this module's public name — nine callers, including
// input_pipeline.cpp. The walk itself now lives in engine_panels.
void* ResolveMainInterface() {
    return acc::engine::ResolveMainInterface();
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

bool VariantRefusal(void* mi, int slot, int index, uint32_t* outStrRef) {
    if (outStrRef) *outStrRef = 0;
    void* desc = DescriptorAddr(mi, slot, index);
    if (!desc) return false;

    int32_t flags = 0;
    int32_t handler = 0;
    if (!ReadInt32(desc, kIfActionFlagsOffset, &flags)) return false;
    if (!ReadInt32(desc, kIfActionHandlerOffset, &handler)) return false;

    // KOTOR 2 item entries carry an UNINITIALISED flag word — the K2
    // appender only writes it for the three equipped-slot items (decompiled
    // 2026-08-12; the ASCII fragments in this log line were heap leftovers).
    // A garbage bit 0 would fake a refusal with a random reason strref, so
    // no refusal verdict for K2 item entries; the engine itself dispatches
    // past the same garbage.
    if (acc::game::IsKotor2() &&
        (handler == kK2MedicalUseHandler ||
         handler == kK2InventoryUseHandler)) {
        acclog::Write("ActionBar",
                      "variant flags slot=%d idx=%d flags=0x%08x (K2 item "
                      "entry — uninitialised, no refusal verdict)",
                      slot, index, static_cast<unsigned>(flags));
        return false;
    }

    // The engine's own predicate, mirrored: usable bit clear, or no handler.
    const bool refused = ((flags & 1) == 0) || (handler == 0);
    // The reason is optional even when the refusal is real — the engine only
    // stamps a string when the code is in range, and beeps either way.
    const uint32_t reason = (static_cast<uint32_t>(flags) >> 1) & 0xf;
    if (refused && outStrRef && reason >= 1 && reason <= 6) {
        *outStrRef = kRefusalStrRefs[reason - 1];
    }

    // Logged on EVERY read, refusal or not. The word itself is the evidence:
    // KOTOR 1 answers a full-health medkit with bit 0 clear and reason 5
    // ("Volle Gesundheit"), KOTOR 2 answers the same item with bit 0 set and
    // dispatches into a handler that silently does nothing. Whether that is
    // Aspyr permitting what KOTOR 1 forbids, or us reading a list KOTOR 2 has
    // not repopulated, is decided by comparing these two words for the same
    // item on the same kind of character — so log it either way.
    acclog::Write("ActionBar",
                  "variant flags slot=%d idx=%d flags=0x%08x handler=%s "
                  "usable=%d reason=%u strref=0x%x",
                  slot, index, static_cast<unsigned>(flags),
                  handler ? "yes" : "NULL", (flags & 1) ? 1 : 0, reason,
                  (outStrRef ? *outStrRef : 0));
    return refused;
}

uint32_t ReadVariantActionId(void* mi, int slot, int index) {
    void* desc = DescriptorAddr(mi, slot, index);
    if (!desc) return 0;
    int32_t v = 0;
    ReadInt32(desc, kIfActionIdOffset, &v);
    return static_cast<uint32_t>(v);
}

void LogDispatchDiag(void* mi, int slot, int index) {
    void* desc = DescriptorAddr(mi, slot, index);
    if (!desc) {
        acclog::Write("ActionBar.Fire", "diag slot=%d idx=%d desc=NULL",
                      slot, index);
        return;
    }
    int32_t id = 0, creature = 0, handler = 0, flags = 0;
    ReadInt32(desc, kIfActionIdOffset,       &id);
    ReadInt32(desc, kIfActionCreatureOffset, &creature);
    ReadInt32(desc, kIfActionHandlerOffset,  &handler);
    ReadInt32(desc, kIfActionFlagsOffset,    &flags);
    // Same resolver family the engine's dispatch gate uses
    // (CClientExoApp::GetGameObject). NOT ResolveClientObjectHandle — its
    // +0xf8 server chain is deliberately unresolved on KOTOR 2.
    void* resolved =
        acc::engine::ResolveClientObject(static_cast<uint32_t>(creature));
    acclog::Write("ActionBar.Fire",
                  "diag slot=%d idx=%d id=0x%08x creature=0x%08x "
                  "clientResolve=%p handler=0x%08x flags=0x%08x",
                  slot, index, static_cast<unsigned>(id),
                  static_cast<unsigned>(creature), resolved,
                  static_cast<unsigned>(handler),
                  static_cast<unsigned>(flags));

    // KOTOR 2 only: the medical handler's two-step target-pick state on the
    // MainInterface — armed flag +0x15230, picked target +0x15234, pending
    // item +0x15238 (witnessed in the K2 medical-use handler 0x0077C780).
    // Armed=1 with target=-1 is the sticky silent state that kills the
    // whole medical column: every further press early-returns on target -1.
    if (acc::game::IsKotor2()) {
        int32_t armed = 0, pickTarget = 0, pendItem = 0;
        ReadInt32(mi, 0x15230, &armed);
        ReadInt32(mi, 0x15234, &pickTarget);
        ReadInt32(mi, 0x15238, &pendItem);
        acclog::Write("ActionBar.Fire",
                      "medpick armed=%d target=0x%08x item=0x%08x",
                      armed, static_cast<unsigned>(pickTarget),
                      static_cast<unsigned>(pendItem));
    }
}

bool EntryIsMedicalK2(void* mi, int slot, int index) {
    if (!acc::game::IsKotor2()) return false;
    void* desc = DescriptorAddr(mi, slot, index);
    if (!desc) return false;
    int32_t handler = 0;
    if (!ReadInt32(desc, kIfActionHandlerOffset, &handler)) return false;
    return handler == kK2MedicalUseHandler;
}

bool SendUseItemRequestK2(uint32_t actionId, void* targetClientCreature) {
    if (!acc::game::IsKotor2()) return false;
    if (actionId == 0 || !targetClientCreature) return false;
    // The client→server "use item" message writer @0x00879AF0 (the one our
    // Diag.UseItemReq hook watches), called exactly the way the engine's
    // medical pick-consume calls it: item id with the action-bar tag bit
    // stripped, user = the TARGET creature (party inventory is shared — the
    // engine models heal-other as "that member uses the item"), position =
    // the target creature's own position field at +0x24. Its `this` is the
    // messaging sink at CClientExoAppInternal+0x150 (getter 0x0073F810).
    //
    // Direct send exists because KOTOR 2's medical items CANNOT be fired
    // through DoPersonalAction from the keyboard at all: the dispatch
    // preamble wipes the two-step pick state before the medical handler
    // reads it, so a key press only ever re-arms the pick and returns —
    // the pick is completed exclusively by the mouse portrait-click path.
    typedef int (__thiscall* PFN_K2SendUseItem)(void* this_, uint32_t itemId,
                                                int p2, int p3,
                                                uint32_t userId,
                                                const float* pos);
    void* internal = acc::engine::GetClientAppInternal();
    if (!internal) return false;
    __try {
        void* sink = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) + 0x150);
        if (!sink) return false;
        unsigned char* cre =
            reinterpret_cast<unsigned char*>(targetClientCreature);
        uint32_t targetId = *reinterpret_cast<uint32_t*>(cre + 4);
        const float* pos  = reinterpret_cast<const float*>(cre + 0x24);
        auto fn = reinterpret_cast<PFN_K2SendUseItem>(0x00879AF0);
        (void)fn(sink, actionId & 0xbfffffffu, 0, 0, targetId, pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar",
                      "SendUseItemRequestK2 SEH-FAULT id=0x%08x",
                      static_cast<unsigned>(actionId));
        return false;
    }
}

void* GetColumnActionButton(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= kColumnCount) return nullptr;
    // CSWGuiMainInterface.field45_0x771c[6], stride 0x71C. action_button
    // is the first member of CSWGuiMainInterfaceAction so its address
    // equals the array-entry address. K2 values listing-witnessed in the
    // K2 PopulateMenus (0x0074CFE0): every Show/SetIcon receiver is
    // `this + i*0x750 + 0x733c`.
    const size_t kFieldArrayBase = acc::off::Pick(0x771c, 0x733c);
    const size_t kColumnStride   = acc::off::Pick(0x71C, 0x750);
    return reinterpret_cast<unsigned char*>(mi) +
           kFieldArrayBase + static_cast<size_t>(slot) * kColumnStride;
}

bool SelectVariant(void* mi, int slot, int index) {
    if (!mi || slot < 0 || slot >= kColumnCount) return false;
    uint32_t actionId = ReadVariantActionId(mi, slot, index);
    if (actionId == 0) return false;
    __try {
        *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(mi) +
            kSelectedActionIdArrayOffset + slot * 4) = actionId;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ActionBar", "SelectVariant SEH-FAULT slot=%d idx=%d",
                      slot, index);
        return false;
    }
}

bool FireSelectedVariant(void* mi, int slot) {
    if (!mi || slot < 0 || slot >= kColumnCount) return false;
    __try {
        auto fn = reinterpret_cast<PFN_DoPersonalAction>(
            kAddrDoPersonalAction);
        // param_2 is unused inside DoPersonalAction (decompile 2026-05-24);
        // variant selection comes from *(mi + 0x1bac + slot*4), which the
        // caller is responsible for stamping via SelectVariant.
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
    void* exoApp   = acc::engine::GetClientApp();
    void* internal = acc::engine::GetClientAppInternal();
    void* guiIn    = acc::engine::ResolveGuiInGame();
    if (!guiIn) {
        acclog::Write("ActionBar.Prep",
            "chain unresolved (exoApp=%p internal=%p guiIn=%p) target=0x%08x",
            exoApp, internal, guiIn, targetClientHandle);
        return false;
    }
    __try {
        auto setTgt = reinterpret_cast<PFN_SetMainInterfaceTarget>(
            kAddrSetMainInterfaceTarget);
        setTgt(guiIn, targetClientHandle, 0u);  // trailing 0: callee purges 8
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
