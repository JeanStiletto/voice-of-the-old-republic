# engine_reads.cpp (1026 lines)

Implementation of engine_reads.h. Grown well past the original GUI-control
readers to cover item property-description reconstruction (calling the
engine's own per-category builder functions), workbench slot/picker state,
and creature Force-point reads.

## Declarations (in source order)

- L11 — `namespace acc::engine`
- L13 — `bool ReadControlNameFields(void* control, const char*& outTip, uint32_t& outTipLen, int& outId)`
- L22 — `void* CallDowncast(void* control, int vtableIndex)`
- L36 — `bool ReadCExoString(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L47 — `uint32_t ReadU32(void* base, size_t offset)`
- L68 — `bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize)`
  note: rejects strref 0/0xFFFFFFFF/>0x100000 before invoking the engine; deliberately leaks the engine-constructed CExoString's c_string.
- L92 — `bool ExtractTextOrStrRef(void* control, size_t cexoOffset, size_t strRefOffset, char* outBuf, size_t bufSize)`
- L100 — `namespace { bool LooksLikeReadableText(const char* buf, size_t len) }`
  note: rejects CP1252 control-range garbage (0x80-0x9F) — action-bar/target-action/radial column buttons leave tooltip_string uninitialised and a stale pointer can yield printable-looking junk.
- L132 — `bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize)`
  note: bounded 8-hop parent walk (cycle guard); strref takes priority over literal per engine decompile; validates literal text via LooksLikeReadableText before accepting it.
- L199 — `bool ReadGuiString(void* control, size_t guiStringPtrOffset, char* outBuf, size_t bufSize)`
- L226 — `bool ExtractTextOrStrRefIndirect(void* control, size_t cexoOffset, size_t strRefOffset, size_t textObjectOffset, char* outBuf, size_t bufSize)`
  note: derives guiStringPtrOffset = cexoOffset-4 to avoid threading an extra parameter through every call site.
- L259 — `bool ReadLabelText(void* label, char* outBuf, size_t bufSize)`
- L279 — `bool ReadButtonText(void* button, char* outBuf, size_t bufSize)`
- L299 — `bool IsToggle(void* control)`
- L312 — `bool IsSlider(void* control)`
  note: SEH-guarded vtable-identity check — a freed-but-non-null pointer during a panel-teardown window previously crashed here (2026-05-11, PartySelection OK button, status new_status=4).
- L322 — `bool IsListBox(void* control)`
- L332 — `bool IsEditbox(void* control)`
  note: accepts kVtableSaveGameEditbox too — same struct layout, engine-side HandleKeyPress override only.
- L346 — `bool ReadToggleState(void* toggle)`
- L350 — `void DumpControlVtable(void* control, char* out, size_t outSize)`
- L361 — `typedef uint32_t (__thiscall* PFN_ClientToServerObjectId)(void*, uint32_t)`, `typedef void* (__thiscall* PFN_GetItemByGameObjectID)(void*, uint32_t)`
- L366 — `uint32_t ClientToServerObjectId(uint32_t clientHandle)`
- L398 — `void* ResolveItemFromClientHandle(uint32_t clientHandle)`
- L431 — `typedef CExoString* (__thiscall* PFN_GetPropertyDescription)(void*, CExoString*)`
- L436 — action-id tag constants: `kActionIdTagFeat`=0x10000000, `kActionIdTagSpell`=0x20000000, `kActionIdTagItem`=0x40000000 (decoded from CreateUsableItemEntry/GetMenuInfo/EnableFeatForMenu)
- L455 — `void* ResolveItemFromServerHandle(uint32_t serverHandle)`
- L486 — `void* GetRulesGlobal()`
- L499 — `bool ResolveFeatDescription(uint32_t featIdx, char* outBuf, size_t bufSize)`
- L528 — `bool ResolveSpellDescription(uint32_t spellId, char* outBuf, size_t bufSize)`
- L565 — `bool ResolveActionDescriptionFromActionId(uint32_t actionId, char* outBuf, size_t bufSize)`
  note: dispatches on the high-nibble tag to item/spell/feat description resolution; other tags return false.
- L588 — `namespace { int ReadItemStackSize(void* item) }`
  note: returns 0 when the infinite-stock bit (bit 2 of bit_flags) is set.
- L615 — `bool ReadItemChargesRaw(void* item, int* outCharges)`
- L631 — `bool IsItemEntryRow(void* control)`
  note: matches kVtableCSWGuiInGameItemEntry OR kVtableCSWGuiStoreItemEntry.
- L646 — `int ReadItemRowStackCount(void* rowControl)`
- L661 — `int ReadItemCharges(void* item)`
- L666 — `int ReadItemRowCharges(void* rowControl)`
- L681 — `int ReadItemStack(void* item)`
- L685 — `void* ItemFromActionId(uint32_t actionId)`
- L691 — `bool IsInventoryItemRow(void* control)`
- L702 — `bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize)`
- L723 — `typedef CExoString* (__thiscall* PFN_GetKeyedPropertyString)(void*, CExoString*, uint8_t)`
- L727 — `bool ReadItemKeyedPropertyString(void* item, uint8_t key, char* outBuf, size_t bufSize)`
- L750 — `namespace { typedef void* (__thiscall* PFN_GetBaseItem)(void*) }`
- L757 — `bool ReadBaseItemFlags(void* item, uint8_t& itemType, uint8_t& weaponType)`
  note: calls CSWItem::GetBaseItem; item_type 0x2e=crystal, 6=grenade skip the whole property block.
- L779 — `void ComputeSectionOffsets(void* item, uint8_t weaponType, size_t& offTags, size_t& offValues, size_t& offProps)`
  note: replays the engine's per-category Add* builders (feat reqs, damage/range/crit/on-hit/size when weapon, attack-mod, defence, misc) into ONE cumulative accumulator so the recorded byte offsets exactly match the canonical GetPropertyDescription string.
- L822 — `bool ReadItemDescriptionViaTlk(void* item, char* outBuf, size_t bufSize, uint32_t& outStrref)`
  note: resolves description via TLK strref, bypassing the sometimes-corrupt inline CExoLocString copy (German umlauts collapsed to 0xFD in some items).
- L838 — `void CopySlice(char* dst, size_t cap, const char* src, size_t start, size_t end)`
- L849 — `bool BuildItemDescriptionBlocks(void* item, ItemDescriptionBlocks* out)`
  note: slices the one canonical `full` string at the computed section offsets; falls back to description-only if offsets diverge (out of order or past fullLen).
- L913 — `void* GetWorkbenchSlotInstalledItem(void* upgradePanel, void* slotControl)`
- L935 — `WorkbenchPickerInfo GetWorkbenchPickerInfo(void* upgradePanel)`
- L974 — `bool ReadCreatureForcePoints(void* clientCreature, int* outCur, int* outMax)`
  note: current FP = (short)(field_0x122 + field_0x124) on the CLIENT CSWCLevelUpStats — the server GetCurrentForcePoints sums different offsets (+0x124/+0x126) on a struct shifted 2 bytes, so the two are not interchangeable.
