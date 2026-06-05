#include "engine_reads.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_player.h"  // kAddrAppManagerPtr
#include "log.h"

namespace acc::engine {

bool ReadControlNameFields(void* control, const char*& outTip,
                           uint32_t& outTipLen, int& outId) {
    auto* base = reinterpret_cast<unsigned char*>(control);
    outTip    = *reinterpret_cast<const char**>(base + 0x28);
    outTipLen = *reinterpret_cast<uint32_t*>   (base + 0x2c);
    outId     = *reinterpret_cast<int*>        (base + kControlIdOffset);
    return outTip && outTipLen > 0;
}

typedef void* (__thiscall* PFN_Downcast)(void* this_);
void* CallDowncast(void* control, int vtableIndex) {
    if (!control) return nullptr;
    __try {
        void** vtable = *reinterpret_cast<void***>(control);
        if (!vtable) return nullptr;
        auto fn = reinterpret_cast<PFN_Downcast>(vtable[vtableIndex]);
        if (!fn) return nullptr;
        return fn(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadCExoString(void* base, size_t offset,
                    char* outBuf, size_t bufSize) {
    auto* p = reinterpret_cast<unsigned char*>(base) + offset;
    const char* s   = *reinterpret_cast<const char**>(p);
    uint32_t    len = *reinterpret_cast<uint32_t*>(p + 4);
    if (!s || len == 0 || len >= bufSize) return false;
    memcpy(outBuf, s, len);
    outBuf[len] = '\0';
    return true;
}

uint32_t ReadU32(void* base, size_t offset) {
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<unsigned char*>(base) + offset);
}

// Out-arg: the engine copy-constructs `out` from a stack-local CExoString,
// allocating a fresh c_string via its own CRT. We copy the string into our
// caller's buffer, then deliberately leak `tmp.c_string` — calling the
// engine's CExoString destructor from our DLL would risk heap mismatch, and
// focus events are low-frequency enough that the leak is negligible across
// a session.
//
// Sanity bounds: we reject strref values that look invalid (-1, >0x100000)
// before invoking the engine, to reduce the rate of expected exceptions.
//
// The engine's GetSimpleString has its own SEH frame (it can raise on
// out-of-range / corrupt indices). A previous attempt to call it from inside
// a hook handler caused our patch to go silent partway through Options —
// presumably the engine's exception unwound through our trampoline and the
// framework disabled the hook on subsequent fires. We now wrap the call in
// __try/__except so any raised exception is contained.
bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize) {
    if (strref == 0 || strref == 0xFFFFFFFF) return false;
    if (strref > 0x100000) return false;  // KOTOR's TLK is well below this

    void* tlk = *reinterpret_cast<void**>(kAddrTlkTablePtr);
    if (!tlk) return false;

    auto fn = reinterpret_cast<PFN_GetSimpleString>(kAddrGetSimpleString);
    CExoString tmp = {nullptr, 0};
    bool ok = false;
    __try {
        fn(tlk, &tmp, strref);
        if (tmp.c_string && tmp.length > 0 && tmp.length < bufSize) {
            memcpy(outBuf, tmp.c_string, tmp.length);
            outBuf[tmp.length] = '\0';
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads", "TLK lookup raised SEH exception for strref=%u", strref);
        ok = false;
    }
    return ok;
}

bool ExtractTextOrStrRef(void* control,
                         size_t cexoOffset, size_t strRefOffset,
                         char* outBuf, size_t bufSize) {
    if (ReadCExoString(control, cexoOffset, outBuf, bufSize)) return true;
    uint32_t strref = ReadU32(control, strRefOffset);
    return LookupTlk(strref, outBuf, bufSize);
}

namespace {

// True iff buf looks like real localised text rather than uninitialised
// memory. The action-bar / target-action / radial column buttons leave
// their CSWGuiControl.tooltip_string slot at +0x28 uninitialised
// (engine renders these buttons via a separate dynamic-text path, so the
// .gui-time CExoString never gets written). What we observed in practice
// is a non-null literal pointer + a small length (3 bytes) yielding
// CP1252 control-range bytes 0x80..0x9F — code points the engine never
// emits in any localised UI string. Rejecting those lets the caller fall
// back to the proper "Keine Beschreibung verfügbar" cue.
bool LooksLikeReadableText(const char* buf, size_t len) {
    if (!buf || len == 0) return false;
    size_t printable = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(buf[i]);
        // ASCII printable + extended Latin range (covers German umlauts
        // 0xE4/0xF6/0xFC/0xDF, accented French/Spanish/Italian, etc.).
        // Excludes 0x00..0x1F (controls), 0x7F (DEL), and 0x80..0x9F
        // (CP1252 control block — never used in localised UI strings).
        if ((c >= 0x20 && c < 0x7F) || c >= 0xA0) {
            ++printable;
        }
    }
    // Require at least one printable byte AND a majority of printable
    // bytes. Short all-garbage strings (the 3-byte case we hit) get
    // rejected; legitimate short tooltips like "OK" keep working.
    return printable > 0 && printable * 2 >= len;
}

}  // namespace

bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize) {
    if (!control || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    // Bound the parent-walk so a malformed/cyclic parent chain can't spin
    // us forever. The deepest in-game panel hierarchy we've observed is
    // ~6 levels (panel → row → embedded button → text); 8 is generous.
    void*    cur   = control;
    void*    last  = nullptr;
    int      hops  = 0;
    while (cur && cur != last && hops < 8) {
        last = cur;
        ++hops;

        uint32_t strref     = 0;
        const char* literal = nullptr;
        uint32_t literalLen = 0;
        void*    parent     = nullptr;

        __try {
            auto* base = reinterpret_cast<unsigned char*>(cur);
            strref  = *reinterpret_cast<uint32_t*>(base + kControlTooltipStrRefOffset);
            literal = *reinterpret_cast<const char**>(base + kControlTooltipStringOffset);
            literalLen = *reinterpret_cast<uint32_t*>(
                base + kControlTooltipStringOffset + 4);
            parent  = *reinterpret_cast<void**>(base + kControlParentOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        // 1. Strref takes priority (engine decompile: when field4_0x24
        //    is non-zero AND a strref lookup succeeds, that wins over
        //    the literal).
        if (strref != 0 && strref != 0xFFFFFFFF) {
            if (LookupTlk(strref, outBuf, bufSize) && outBuf[0]) {
                return true;
            }
        }

        // 2. Literal tooltip_string. Validate it looks like real text —
        //    action-bar / target-action / radial column buttons leave
        //    this slot uninitialised and the engine never wipes it on
        //    .gui load, so a stale non-null pointer + small length can
        //    return CP1252 control-range garbage. Drop garbage and keep
        //    bubbling so the caller's "no tooltip" fallback fires.
        if (literal && literalLen > 0 && literalLen < bufSize) {
            __try {
                memcpy(outBuf, literal, literalLen);
                outBuf[literalLen] = '\0';
                if (LooksLikeReadableText(outBuf, literalLen)) {
                    return true;
                }
                outBuf[0] = '\0';
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                outBuf[0] = '\0';
                // Fall through to parent walk — corrupt literal pointer.
            }
        }

        // 3. Bubble up to parent and retry (engine recurses via
        //    parent_control->vtable->DisplayToolTip).
        cur = parent;
    }

    return false;
}

bool ReadGuiString(void* control, size_t guiStringPtrOffset,
                   char* outBuf, size_t bufSize) {
    if (!control || bufSize < 2) return false;
    bool got = false;
    __try {
        void* guiString = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + guiStringPtrOffset);
        if (!guiString) return false;
        uintptr_t gsVtable = *reinterpret_cast<uintptr_t*>(guiString);
        if (gsVtable != kVtableCAurGUIStringInternal) return false;
        char* str = *reinterpret_cast<char**>(
            reinterpret_cast<unsigned char*>(guiString) + kAurGuiStringCStrOffset);
        if (!str) return false;
        size_t len = 0;
        while (len < bufSize - 1 && str[len] != '\0') ++len;
        if (len == 0) return false;
        memcpy(outBuf, str, len);
        outBuf[len] = '\0';
        got = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads", "ReadGuiString SEH for control=%p offset=0x%x",
                      control, (unsigned)guiStringPtrOffset);
        got = false;
    }
    return got;
}

bool ExtractTextOrStrRefIndirect(void* control,
                                 size_t cexoOffset, size_t strRefOffset,
                                 size_t textObjectOffset,
                                 char* outBuf, size_t bufSize) {
    // gui_string offset for label and button differ by inline CSWGuiText
    // start offset; derive from cexoOffset to avoid threading another
    // parameter through every call site (gui_string ptr is at
    // text.+0x14 = (cexo - 0x18) + 0x14 = cexo - 4).
    size_t guiStringPtrOffset = cexoOffset - 4;
    if (ReadGuiString(control, guiStringPtrOffset, outBuf, bufSize)) {
        return true;
    }
    if (ExtractTextOrStrRef(control, cexoOffset, strRefOffset, outBuf, bufSize)) {
        return true;
    }
    bool got = false;
    __try {
        void* textObj = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + textObjectOffset);
        if (textObj) {
            got = ExtractTextOrStrRef(textObj,
                                      kTextObjectTextOffset,
                                      kTextObjectStrRefOffset,
                                      outBuf, bufSize);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads", "text_object indirection SEH for control=%p "
                      "(textObjectOffset=0x%x)", control, (unsigned)textObjectOffset);
        got = false;
    }
    return got;
}

bool ReadLabelText(void* label, char* outBuf, size_t bufSize) {
    if (!label || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        if (ReadGuiString(label, kLabelGuiStringPtrOffset,
                          outBuf, bufSize) && outBuf[0] != '\0') {
            return true;
        }
        if (ExtractTextOrStrRefIndirect(
                label, kLabelTextOffset, kLabelStrRefOffset,
                kLabelTextObjectOffset, outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

bool ReadButtonText(void* button, char* outBuf, size_t bufSize) {
    if (!button || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        if (ReadGuiString(button, kButtonGuiStringPtrOffset,
                          outBuf, bufSize) && outBuf[0] != '\0') {
            return true;
        }
        if (ExtractTextOrStrRefIndirect(
                button, kButtonTextOffset, kButtonStrRefOffset,
                kButtonTextObjectOffset, outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

bool IsToggle(void* control) {
    return CallDowncast(control, kVtableAsButtonToggle) != nullptr;
}

// Vtable-identity predicates run from per-tick menu monitors. A null-check
// alone isn't enough: panel-teardown windows (e.g. modal close → area load)
// can leave a freed-but-non-null control pointer in cached monitor state
// for one extra Dispatch() tick. Crash analysed 2026-05-11 (dump
// swkotor.exe.14028.dmp): IsSlider faulted at the vtable read on a freed
// PartySelection OK button right after `SubScreen.Status new_status=4`.
// SEH-guard the dereference so a stale pointer returns false (same as
// a real type mismatch) instead of access-violation-ing the process.
// CallDowncast above uses the same pattern for the IsToggle path.
bool IsSlider(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == kVtableSlider;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsListBox(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == kVtableListBox;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsEditbox(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == kVtableEditbox;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadToggleState(void* toggle) {
    return (ReadU32(toggle, kButtonToggleStateOffset) & 1u) != 0;
}

void DumpControlVtable(void* control, char* out, size_t outSize) {
    void** vtable = *reinterpret_cast<void***>(control);
    if (!vtable) {
        snprintf(out, outSize, "vtable=NULL");
        return;
    }
    snprintf(out, outSize,
             "vtable=%p [0]=%p [4]=%p [20]=%p [22]=%p",
             vtable, vtable[0], vtable[4], vtable[20], vtable[22]);
}

typedef uint32_t (__thiscall* PFN_ClientToServerObjectId)(void* this_,
                                                           uint32_t handle);
typedef void*    (__thiscall* PFN_GetItemByGameObjectID)(void* this_,
                                                         uint32_t handle);

uint32_t ClientToServerObjectId(uint32_t clientHandle) {
    if (clientHandle == 0 || clientHandle == 0xffffffff) return 0;
    void* appMgr = nullptr;
    __try {
        appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!appMgr) return 0;

    void* serverApp = nullptr;
    __try {
        serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) +
            kAppManagerServerExoAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!serverApp) return 0;

    uint32_t serverHandle = 0;
    __try {
        auto fn = reinterpret_cast<PFN_ClientToServerObjectId>(
            kAddrServerExoAppClientToServerObjectId);
        serverHandle = fn(serverApp, clientHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (serverHandle == 0 || serverHandle == 0xffffffff) return 0;
    return serverHandle;
}

void* ResolveItemFromClientHandle(uint32_t clientHandle) {
    uint32_t serverHandle = ClientToServerObjectId(clientHandle);
    if (serverHandle == 0) return nullptr;

    void* appMgr = nullptr;
    __try {
        appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!appMgr) return nullptr;

    void* serverApp = nullptr;
    __try {
        serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) +
            kAppManagerServerExoAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!serverApp) return nullptr;

    void* item = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_GetItemByGameObjectID>(
            kAddrServerExoAppGetItemByGameObjectID);
        item = fn(serverApp, serverHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return item;
}

typedef CExoString* (__thiscall* PFN_GetPropertyDescription)(void* this_,
                                                              CExoString* out);

// CSWGuiInterfaceAction descriptors encode an action-type tag in the
// high nibble of the action_id at +0x08; the low 28 bits carry the
// category-specific lookup key. Tags decoded via decompile of the
// CSWCCreature entry-creators:
//
//   0x10000000  feat        — CSWCCreature::EnableFeatForMenu      @0x00618a30
//                             ("action_id = feat_id | 0x10000000")
//   0x20000000  force power — CSWCCreatureStats_ClassInfo::GetMenuInfo
//                             @0x0064a870 ("action_id = spell_id | 0x20000000")
//   0x40000000  item        — CSWCCreature::CreateUsableItemEntry  @0x006193a0
//                             ("action_id = server_item.game_object.id | 0x40000000")
//
// Other categories (attack verbs, door open/unlock, computer hack, etc.)
// don't carry a separately addressable description — they're plain verbs
// the engine never surfaces extra text for, so we let the caller fall
// back to the localised "no description" cue.
constexpr uint32_t kActionIdTagMask     = 0xF0000000;
constexpr uint32_t kActionIdTagFeat     = 0x10000000;
constexpr uint32_t kActionIdTagSpell    = 0x20000000;
constexpr uint32_t kActionIdTagItem     = 0x40000000;

void* ResolveItemFromServerHandle(uint32_t serverHandle) {
    if (serverHandle == 0 || serverHandle == 0xffffffff) return nullptr;
    void* appMgr = nullptr;
    __try {
        appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!appMgr) return nullptr;

    void* serverApp = nullptr;
    __try {
        serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) +
            kAppManagerServerExoAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!serverApp) return nullptr;

    void* item = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_GetItemByGameObjectID>(
            kAddrServerExoAppGetItemByGameObjectID);
        item = fn(serverApp, serverHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return item;
}

void* GetRulesGlobal() {
    void* rules = nullptr;
    __try {
        rules = *reinterpret_cast<void**>(kAddrRulesGlobal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return rules;
}

typedef void* (__thiscall* PFN_RulesGetFeat)(void* rules, uint16_t featIdx);
typedef void* (__thiscall* PFN_FeatGetDescriptionText)(void* feat, CExoString* out);

bool ResolveFeatDescription(uint32_t featIdx, char* outBuf, size_t bufSize) {
    void* rules = GetRulesGlobal();
    if (!rules) return false;

    void* feat = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_RulesGetFeat>(kAddrCSWRulesGetFeat);
        feat = fn(rules, static_cast<uint16_t>(featIdx));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!feat) return false;

    CExoString tmp = {nullptr, 0};
    __try {
        auto fn = reinterpret_cast<PFN_FeatGetDescriptionText>(
            kAddrCSWFeatGetDescriptionText);
        fn(feat, &tmp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!tmp.c_string || tmp.length == 0 || tmp.length >= bufSize) return false;
    memcpy(outBuf, tmp.c_string, tmp.length);
    outBuf[tmp.length] = '\0';
    // c_string is a heap CRT-mismatched alloc by the engine; same leak
    // rule as ReadItemPropertyDescription.
    return true;
}

typedef void* (__thiscall* PFN_SpellArrayGetSpell)(void* spells, int spellId);

bool ResolveSpellDescription(uint32_t spellId, char* outBuf, size_t bufSize) {
    void* rules = GetRulesGlobal();
    if (!rules) return false;

    void* spellArray = nullptr;
    __try {
        spellArray = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(rules) + kRulesSpellsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!spellArray) return false;

    void* spell = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_SpellArrayGetSpell>(
            kAddrCSWSpellArrayGetSpell);
        spell = fn(spellArray, static_cast<int>(spellId));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!spell) return false;

    uint32_t descStrRef = 0;
    __try {
        descStrRef = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(spell) +
            kSpellDescriptionStrRefOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (descStrRef == 0 || descStrRef == 0xffffffff) return false;
    return LookupTlk(descStrRef, outBuf, bufSize);
}

bool ResolveActionDescriptionFromActionId(uint32_t actionId,
                                          char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    if (actionId == 0 || actionId == 0xffffffff) return false;

    uint32_t tag   = actionId & kActionIdTagMask;
    uint32_t lowId = actionId & ~kActionIdTagMask;
    switch (tag) {
        case kActionIdTagItem: {
            void* item = ResolveItemFromServerHandle(lowId);
            if (!item) return false;
            return ReadItemPropertyDescription(item, outBuf, bufSize);
        }
        case kActionIdTagSpell:
            return ResolveSpellDescription(lowId, outBuf, bufSize);
        case kActionIdTagFeat:
            return ResolveFeatDescription(lowId, outBuf, bufSize);
        default:
            return false;
    }
}

namespace {

// Read CSWSItem.stack_size (2 bytes) and bit_flags (4 bytes). Mirrors the
// store-side ReadItemStock helper but doesn't return the infinite-stock
// flag separately — callers that need that distinction use the store
// path. Returns 0 on fault or infinite-stock items.
int ReadItemStackSize(void* item) {
    if (!item) return 0;
    uint32_t bitFlags = 0;
    uint16_t stack = 0;
    __try {
        bitFlags = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(item) + kSwsItemBitFlagsOffset);
        stack = *reinterpret_cast<uint16_t*>(
            reinterpret_cast<unsigned char*>(item) + kSwsItemStackSizeOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (bitFlags & kSwsItemInfiniteStockBit) return 0;
    return (int)stack;
}

bool IsItemEntryRow(void* control) {
    if (!control) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    auto v = reinterpret_cast<uintptr_t>(vt);
    return v == kVtableCSWGuiInGameItemEntry ||
           v == kVtableCSWGuiStoreItemEntry;
}

}  // namespace

int ReadItemRowStackCount(void* rowControl) {
    if (!IsItemEntryRow(rowControl)) return 0;
    uint32_t handle = 0;
    __try {
        handle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(rowControl) +
            kStoreItemEntryObjIdOffset);  // same offset on both row vtables
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    void* item = ResolveItemFromClientHandle(handle);
    if (!item) return 0;
    return ReadItemStackSize(item);
}

bool IsInventoryItemRow(void* control) {
    if (!control) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiInGameItemEntry;
}

bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize) {
    if (!item || !outBuf || bufSize < 2) return false;
    CExoString tmp = {nullptr, 0};
    bool ok = false;
    __try {
        auto fn = reinterpret_cast<PFN_GetPropertyDescription>(
            kAddrCSWSItemGetPropertyDescription);
        fn(item, &tmp);
        if (tmp.c_string && tmp.length > 0 && tmp.length < bufSize) {
            memcpy(outBuf, tmp.c_string, tmp.length);
            outBuf[tmp.length] = '\0';
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads",
                      "GetPropertyDescription SEH for item=%p", item);
        ok = false;
    }
    return ok;
}

void* GetWorkbenchSlotInstalledItem(void* upgradePanel, void* slotControl) {
    if (!upgradePanel || !slotControl) return nullptr;
    void* item = nullptr;
    __try {
        int customValue = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(slotControl) +
            kUpgradeSlotCustomValueOff);
        // Guard the index — the array holds at most 4 mod pointers (one per
        // slot). custom_value should always be 0..3 for a real slot button.
        if (customValue < 0 || customValue > 3) return nullptr;
        item = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(upgradePanel) +
            kUpgradeSlotInstalledItemsOff + customValue * 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads",
                      "GetWorkbenchSlotInstalledItem SEH panel=%p slot=%p",
                      upgradePanel, slotControl);
        return nullptr;
    }
    return item;
}

}  // namespace acc::engine
