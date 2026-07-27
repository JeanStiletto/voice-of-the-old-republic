# engine_reads.h (256 lines)

SEH-guarded read helpers for KOTOR GUI controls, item property text, and
Force-point stats. Every deref is __try-wrapped since hook handlers may run
during engine mid-teardown. Documents the gui_string ground-truth rationale
(CSWGuiText::Draw reads ONLY through gui_string) and the vtable-check
before dereffing gui_string (chargen Class buttons transiently hold
garbage that would /GS-fastfail without it).

## Declarations (in source order)

- L12 — `namespace acc::engine`
- L16 — `bool ReadControlNameFields(void* control, const char*& outTip, uint32_t& outTipLen, int& outId)`
- L27 — `void* CallDowncast(void* control, int vtableIndex)`
  note: SEH-wrapped vtable[index](control); faults map to nullptr so stale freed controls during teardown are treated as type-mismatch, not a crash.
- L30 — `bool ReadCExoString(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L32 — `uint32_t ReadU32(void* base, size_t offset)`
- L36 — `bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize)`
  note: SEH-wraps the engine's GetSimpleString call — an uncaught exception there previously unwound through our trampoline and silently disabled the whole hook.
- L43 — `bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize)`
  note: mirrors CSWGuiControl::DisplayToolTip priority — strref → literal tooltip_string → bubble to parent_control.
- L59 — `bool ReadGuiString(void* control, size_t guiStringPtrOffset, char* outBuf, size_t bufSize)`
  note: guiStringPtrOffset is 0xE4 for label, 0x168 for button; vtable-validates CAurGUIStringInternal before deref.
- L63 — `bool ExtractTextOrStrRef(void* control, size_t cexoOffset, size_t strRefOffset, char* outBuf, size_t bufSize)`
- L75 — `bool ExtractTextOrStrRefIndirect(void* control, size_t cexoOffset, size_t strRefOffset, size_t textObjectOffset, char* outBuf, size_t bufSize)`
  note: four-path — gui_string, inline CExoString, strref TLK, text_object indirection; gui_string first because overridden subclasses (CSWGuiInGameMenu icon labels) leave the other three empty.
- L85 — `bool ReadLabelText(void* label, char* outBuf, size_t bufSize)`
- L89 — `inline bool ReadLabelTextAt(void* panel, size_t offset, char* outBuf, size_t bufSize)`
- L102 — `bool ReadButtonText(void* button, char* outBuf, size_t bufSize)`
- L106 — `bool IsToggle(void* control)`, `IsSlider`, `IsListBox`, `IsEditbox`
- L113 — `bool ReadToggleState(void* toggle)`
- L117 — `void DumpControlVtable(void* control, char* out, size_t outSize)`
- L123 — `uint32_t ClientToServerObjectId(uint32_t clientHandle)`
- L129 — `void* ResolveItemFromClientHandle(uint32_t clientHandle)`
- L134 — `bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize)`
  note: engine-allocated heap c_string deliberately leaked (CRT-mismatch across the DLL/EXE boundary).
- L141 — `struct ItemDescriptionBlocks { char tags[2048]; char values[2048]; char properties[4096]; char description[4096]; }`
- L151 — `bool BuildItemDescriptionBlocks(void* item, ItemDescriptionBlocks* out)`
  note: reconstructs the four screen-reader nav blocks by replaying the engine's own per-category builders into a cumulative accumulator and slicing byte offsets — matches the canonical rendered text exactly rather than diverging by re-emitting sections separately.
- L158 — `bool ReadItemKeyedPropertyString(void* item, uint8_t key, char* outBuf, size_t bufSize)`
  note: the workbench's per-slot "Spezielle Eigenschaften" bonus line — text GetPropertyDescription omits for crystals.
- L168 — `void* GetWorkbenchSlotInstalledItem(void* upgradePanel, void* slotControl)`
- L180 — `struct WorkbenchPickerInfo { bool valid; bool isColorSlot; int minSel; int installedRow; }`
- L186 — `WorkbenchPickerInfo GetWorkbenchPickerInfo(void* upgradePanel)`
  note: saber color slot (custom_value==1) has no remove entry (minSel 0); power/non-color slot's row 0 is a hidden 0x7f000000 remove entry (minSel 1).
- L203 — `bool ResolveActionDescriptionFromActionId(uint32_t actionId, char* outBuf, size_t bufSize)`
  note: dispatches on the action_id high-nibble tag (0x1x feat, 0x2x force power, 0x4x item); other categories (attack verbs, door toggle, computer hack) return false — plain verbs with no extra engine text.
- L213 — `int ReadItemRowStackCount(void* rowControl)`
  note: >1 stackable, 1 single (caller stays silent), 0 not-an-item-row/unresolved/infinite-stock.
- L222 — `int ReadItemCharges(void* item)`, `ReadItemRowCharges(void* rowControl)`
  note: charged items (max_charges>0) never coincide with a stack suffix.
- L228 — `int ReadItemStack(void* item)`
- L235 — `void* ItemFromActionId(uint32_t actionId)`
- L240 — `bool IsInventoryItemRow(void* control)`
  note: CSWGuiInGameItemEntry only — store rows get a price+stock suffix from menus_store instead.
- L254 — `bool ReadCreatureForcePoints(void* clientCreature, int* outCur, int* outMax)`
  note: walks CSWCCreature+0x2f8 → CSWCLevelUpStats; live current FP is the SUM of the two shorts at +0x122/+0x124, NOT the +0x120 "force_points" base field; *outMax==0 is a valid "not a Force user" result, not a failure signal.
