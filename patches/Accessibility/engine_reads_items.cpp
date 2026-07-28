// item / creature domain readers.
//
// Split out of engine_reads.cpp by the Phase-1 structure pass (refactoring
// candidate 4). engine_reads.cpp kept the generic GUI-control readers
// (control text, tooltips, TLK lookup, toggle/slider/listbox predicates);
// this file owns the domain layer that grew on top of them:
//
//   * client/server handle translation and item resolution
//   * feat / spell / action description resolvers
//   * item stack, charge and inventory-row reads
//   * the item property-description block reconstruction
//   * creature Force-point reads
//
// Declarations stay in engine_reads.h - splitting that header is a
// separate, deferred item, so every existing includer is unaffected.

#include "engine_reads.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_player.h"  // kAddrAppManagerPtr
#include "log.h"

namespace acc::engine {

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

// Read CSWSItem.charges (+0x258) when the item is a charged item
// (max_charges @ +0x25c > 0). Returns the current charge count via
// *outCharges and true; returns false (item not charge-based / fault)
// otherwise. Charged items can't stack, so this is orthogonal to
// ReadItemStackSize.
bool ReadItemChargesRaw(void* item, int* outCharges) {
    if (!item) return false;
    uint32_t charges = 0, maxCharges = 0;
    __try {
        charges = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(item) + kSwsItemChargesOffset);
        maxCharges = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(item) + kSwsItemMaxChargesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (maxCharges == 0) return false;
    if (outCharges) *outCharges = (int)charges;
    return true;
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

int ReadItemCharges(void* item) {
    int charges = -1;
    return ReadItemChargesRaw(item, &charges) ? charges : -1;
}

int ReadItemRowCharges(void* rowControl) {
    if (!IsItemEntryRow(rowControl)) return -1;
    uint32_t handle = 0;
    __try {
        handle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(rowControl) +
            kStoreItemEntryObjIdOffset);  // same offset on both row vtables
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    void* item = ResolveItemFromClientHandle(handle);
    if (!item) return -1;
    return ReadItemCharges(item);
}

int ReadItemStack(void* item) {
    return ReadItemStackSize(item);
}

void* ItemFromActionId(uint32_t actionId) {
    if (actionId == 0 || actionId == 0xffffffff) return nullptr;
    if ((actionId & kActionIdTagMask) != kActionIdTagItem) return nullptr;
    return ResolveItemFromServerHandle(actionId & ~kActionIdTagMask);
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

typedef CExoString* (__thiscall* PFN_GetKeyedPropertyString)(void* this_,
                                                             CExoString* out,
                                                             uint8_t key);

bool ReadItemKeyedPropertyString(void* item, uint8_t key,
                                 char* outBuf, size_t bufSize) {
    if (!item || !outBuf || bufSize < 2) return false;
    CExoString tmp = {nullptr, 0};
    bool ok = false;
    __try {
        auto fn = reinterpret_cast<PFN_GetKeyedPropertyString>(
            kAddrCSWSItemGetKeyedPropertyString);
        fn(item, &tmp, key);
        if (tmp.c_string && tmp.length > 0 && tmp.length < bufSize) {
            memcpy(outBuf, tmp.c_string, tmp.length);
            outBuf[tmp.length] = '\0';
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads",
                      "GetKeyedPropertyString SEH for item=%p key=%u",
                      item, (unsigned)key);
        ok = false;
    }
    return ok;
}

namespace {

typedef void* (__thiscall* PFN_GetBaseItem)(void* item);

// Read item_type / weapon_type via CSWItem::GetBaseItem (the CSWItem subobject
// is at offset 0 of CSWSItem, so the item pointer is a valid `this`). Offsets
// are CMP-verified from the GetPropertyDescription disassembly.
bool ReadBaseItemFlags(void* item, uint8_t& itemType, uint8_t& weaponType) {
    __try {
        auto fn = reinterpret_cast<PFN_GetBaseItem>(kAddrCSWItemGetBaseItem);
        auto* base = reinterpret_cast<unsigned char*>(fn(item));
        if (!base) return false;
        itemType   = *(base + kBaseItemItemTypeOffset);
        weaponType = *(base + kBaseItemWeaponTypeOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Append the engine's own per-category builders to ONE accumulator, in the same
// order and with the same weapon guard GetPropertyDescription uses, recording
// the visible (strlen) length after each section. The result is the byte offsets
// at which Tags / Values / Properties end inside the canonical
// GetPropertyDescription string — so the caller can slice that string rather
// than re-emit each section (separate accumulators diverge from the canonical
// text by a byte or two; one cumulative accumulator matches it exactly). The
// accumulator's heap c_string is leaked (CRT-mismatch rule). offTags/offValues/
// offProps are left at their incoming values on fault.
void ComputeSectionOffsets(void* item, uint8_t weaponType,
                           size_t& offTags, size_t& offValues,
                           size_t& offProps) {
    CExoString acc = {nullptr, 0};
    __try {
        reinterpret_cast<PFN_CExoStringCtor>(kAddrCExoStringDefaultCtor)(&acc);

        reinterpret_cast<PFN_AddItemProperty>(
            kAddrItemAddFeatRequirements)(item, &acc);
        offTags = acc.c_string ? strlen(acc.c_string) : 0;

        if (weaponType != 0) {
            reinterpret_cast<PFN_AddItemProperty>(
                kAddrItemAddDamageProperties)(item, &acc);
            reinterpret_cast<PFN_AddItemProperty>(
                kAddrItemAddRangeProperties)(item, &acc);
            reinterpret_cast<PFN_AddItemProperty>(
                kAddrItemAddCriticalThreatProps)(item, &acc);
            reinterpret_cast<PFN_AddItemProperty>(
                kAddrItemAddOnHitProperties)(item, &acc);
            reinterpret_cast<PFN_AddItemProperty>(
                kAddrItemAddWeaponSizeProperties)(item, &acc);
        }
        reinterpret_cast<PFN_AddItemProperty>(
            kAddrItemAddAttackModifierProps)(item, &acc);
        reinterpret_cast<PFN_AddItemProperty>(
            kAddrItemAddDefenceProperties)(item, &acc);
        offValues = acc.c_string ? strlen(acc.c_string) : offTags;

        reinterpret_cast<PFN_AddItemProperty>(
            kAddrItemAddMiscellaneousProps)(item, &acc);
        offProps = acc.c_string ? strlen(acc.c_string) : offValues;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads",
                      "ComputeSectionOffsets SEH for item=%p", item);
    }
}

// Resolve the item's identified-description text through the TLK strref,
// bypassing the (sometimes corrupt) inline CExoLocString substring that
// GetPropertyDescription/GetString prefer. Returns false when there's no strref
// (-1 sentinel) or the lookup fails / is empty. outStrref reports the strref for
// logging.
bool ReadItemDescriptionViaTlk(void* item, char* outBuf, size_t bufSize,
                               uint32_t& outStrref) {
    outStrref = 0xFFFFFFFF;
    if (!item || !outBuf || bufSize < 2) return false;
    __try {
        outStrref = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(item) +
            kItemDescriptionLocStringOffset + kExoLocStringStrRefOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (outStrref == 0xFFFFFFFF) return false;  // no TLK entry for this locstring
    return LookupTlk(outStrref, outBuf, bufSize) && outBuf[0] != '\0';
}

// Copy src[start..end) into dst (NUL-terminated, bounded by cap).
void CopySlice(char* dst, size_t cap, const char* src, size_t start, size_t end) {
    if (!dst || cap == 0) return;
    if (end < start) end = start;
    size_t n = end - start;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src + start, n);
    dst[n] = '\0';
}

}  // namespace

bool BuildItemDescriptionBlocks(void* item, ItemDescriptionBlocks* out) {
    if (!item || !out) return false;
    out->tags[0] = out->values[0] = out->properties[0] =
        out->description[0] = '\0';

    // Canonical text — exactly what the game renders. Every spoken block is a
    // slice of THIS string; we never re-emit a section, so the bytes (and their
    // CP-1252 encoding) always match the game.
    char full[8192];
    if (!ReadItemPropertyDescription(item, full, sizeof(full))) return false;
    size_t fullLen = strlen(full);

    uint8_t itemType = 0, weaponType = 0;
    bool haveFlags = ReadBaseItemFlags(item, itemType, weaponType);

    // Find where Tags / Values / Properties end inside `full` by replaying the
    // engine's builder sequence into one accumulator. Crystals (0x2e) and
    // grenades (6) skip the whole property block — as does a missing base item —
    // so all offsets stay 0 and the entire string is the description.
    size_t offTags = 0, offValues = 0, offProps = 0;
    if (haveFlags && itemType != 0x2e && itemType != 6) {
        ComputeSectionOffsets(item, weaponType, offTags, offValues, offProps);
    }

    // Guard against any divergence from `full` (offsets past the string, or out
    // of order): fall back to the whole string as the description.
    if (offTags > offValues || offValues > offProps || offProps > fullLen) {
        acclog::Write("Engine.Reads",
                      "BuildItemDescriptionBlocks item=%p offsets diverged "
                      "(tags=%zu values=%zu props=%zu fullLen=%zu); "
                      "description-only fallback",
                      item, offTags, offValues, offProps, fullLen);
        offTags = offValues = offProps = 0;
    }

    CopySlice(out->tags,       sizeof(out->tags),       full, 0,         offTags);
    CopySlice(out->values,     sizeof(out->values),     full, offTags,   offValues);
    CopySlice(out->properties, sizeof(out->properties), full, offValues, offProps);

    // Description: prefer the TLK strref (clean cp1252) over the inline copy that
    // GetPropertyDescription emits — some German items have a corrupt inline
    // description (umlauts collapsed to 0xFD). Fall back to the GetPropertyDescription
    // tail when there's no usable strref.
    uint32_t descStrref = 0xFFFFFFFF;
    bool descViaTlk = ReadItemDescriptionViaTlk(item, out->description,
                                                sizeof(out->description),
                                                descStrref);
    if (!descViaTlk) {
        CopySlice(out->description, sizeof(out->description), full, offProps,
                  fullLen);
    }

    acclog::Write("Engine.Reads",
                  "BuildItemDescriptionBlocks item=%p itemType=%u weaponType=%u "
                  "fullLen=%zu offsets(tags=%zu values=%zu props=%zu) "
                  "descStrref=0x%x descViaTlk=%d "
                  "lens(tags=%zu values=%zu props=%zu desc=%zu)",
                  item, (unsigned)itemType, (unsigned)weaponType, fullLen,
                  offTags, offValues, offProps, descStrref, descViaTlk ? 1 : 0,
                  strlen(out->tags), strlen(out->values),
                  strlen(out->properties), strlen(out->description));
    return fullLen > 0 || out->description[0] != '\0';
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

WorkbenchPickerInfo GetWorkbenchPickerInfo(void* upgradePanel) {
    WorkbenchPickerInfo info;
    if (!upgradePanel) return info;

    void* slotBtn = nullptr;
    int customValue = -1;
    __try {
        slotBtn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(upgradePanel) +
            kUpgradeActiveSlotOff);
        if (slotBtn) {
            customValue = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(slotBtn) +
                kUpgradeSlotCustomValueOff);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads",
                      "GetWorkbenchPickerInfo SEH panel=%p", upgradePanel);
        return info;
    }
    if (!slotBtn) return info;

    info.valid = true;
    if (customValue == 1) {
        // Saber color slot: row 0 is the current colour crystal, every row is
        // a real choice, no remove entry.
        info.isColorSlot  = true;
        info.minSel       = 0;
        info.installedRow = 0;
    } else {
        // Power / non-color saber slot: row 0 is the 0x7f000000 remove entry;
        // row 1 is the installed crystal when the slot is occupied.
        info.minSel       = 1;
        void* installed = GetWorkbenchSlotInstalledItem(upgradePanel, slotBtn);
        info.installedRow = installed ? 1 : -1;
    }
    return info;
}

bool ReadCreatureForcePoints(void* clientCreature, int* outCur, int* outMax) {
    if (outCur) *outCur = 0;
    if (outMax) *outMax = 0;
    if (!clientCreature) return false;

    // CSWCCreature+0x2f8 -> CSWCLevelUpStats* (embeds CSWCCreatureStats).
    // Same chain as combat_query's HP reads; force pool lives further into
    // the same struct.
    //
    //   +0x11e  short  max_force_points   (cached computed max — LIVE)
    //   +0x120  short  force_points       (BASE term fed into GetMaxForcePoints,
    //                                       NOT the live current pool — ignore)
    //   +0x122  short  field85_0x122  ┐ live current force = sum of these two,
    //   +0x124  short  field86_0x124  ┘ exactly as the engine's own
    //                                   CSWGuiInGameCharacter::SetStats
    //                                   (@0x006afda0) computes the FP label off
    //                                   this client struct. (Note the server
    //                                   GetCurrentForcePoints sums +0x124/+0x126
    //                                   — the server stats struct is shifted 2
    //                                   bytes from this client one, so reuse of
    //                                   the server offsets here reads garbage.)
    constexpr size_t kLvlUpStatsOffset = 0x2f8;
    constexpr size_t kMaxForceOffset   = 0x11e;
    constexpr size_t kCurForceLoOffset = 0x122;
    constexpr size_t kCurForceHiOffset = 0x124;

    void* lvlUpStats = nullptr;
    __try {
        lvlUpStats = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientCreature) +
            kLvlUpStatsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!lvlUpStats) return false;

    __try {
        unsigned char* stats = reinterpret_cast<unsigned char*>(lvlUpStats);
        int maxFp = static_cast<int>(
            *reinterpret_cast<short*>(stats + kMaxForceOffset));
        // Match the engine: (short)(field_0x124 + field_0x126).
        int curFp = static_cast<int>(static_cast<short>(
            *reinterpret_cast<short*>(stats + kCurForceLoOffset) +
            *reinterpret_cast<short*>(stats + kCurForceHiOffset)));
        if (outMax) *outMax = maxFp;
        if (outCur) *outCur = curFp;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

}  // namespace acc::engine
