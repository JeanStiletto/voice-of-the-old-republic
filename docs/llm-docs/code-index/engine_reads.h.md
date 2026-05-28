# engine_reads.h (124 lines)

SEH-guarded read helpers for KOTOR GUI controls. Safe from hook handlers that may run during engine mid-teardown — every deref is __try-wrapped. Documents the gui_string ground-truth rationale (CSWGuiText::Draw reads ONLY through gui_string; CExoString/strref are ignored at draw time), and the vtable-check before dereffing the gui_string pointer (chargen Class buttons transiently hold garbage at +0x168 which would fastfail under /GS if deref'd without the vtable guard).

## Declarations (in source order)

- L12 — `namespace acc::engine`
- L16 — `bool ReadControlNameFields(void* control, const char*& outTip, uint32_t& outTipLen, int& outId)`
  note: reads +0x28 tooltip c_string, +0x2c length, +0x50 id; returns true when tooltip is non-empty
- L27 — `void* CallDowncast(void* control, int vtableIndex)`
  note: SEH-wrapped vtable[index](control) thiscall; faults map to nullptr so stale freed controls during teardown are treated as type-mismatch
- L30 — `bool ReadCExoString(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L32 — `uint32_t ReadU32(void* base, size_t offset)`
- L36 — `bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize)`
  note: rejects strref 0/0xFFFFFFFF/>0x100000; SEH-wraps the engine's GetSimpleString call to contain exception unwinds that would disable hooks
- L43 — `bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize)`
  note: mirrors CSWGuiControl::DisplayToolTip priority (strref → literal tooltip_string → bubble to parent_control, max 8 hops)
- L59 — `bool ReadGuiString(void* control, size_t guiStringPtrOffset, char* outBuf, size_t bufSize)`
  note: vtable-validates CAurGUIStringInternal before deref; guiStringPtrOffset = 0xE4 for label, 0x168 for button
- L63 — `bool ExtractTextOrStrRef(void* control, size_t cexoOffset, size_t strRefOffset, char* outBuf, size_t bufSize)`
  note: inline CExoString then TLK strref
- L75 — `bool ExtractTextOrStrRefIndirect(void* control, size_t cexoOffset, size_t strRefOffset, size_t textObjectOffset, char* outBuf, size_t bufSize)`
  note: four-path: gui_string (ground-truth), inline CExoString, strref TLK, text_object indirection
- L82 — `bool IsToggle(void* control)`
- L83 — `bool IsSlider(void* control)`
- L84 — `bool IsListBox(void* control)`
- L85 — `bool IsEditbox(void* control)`
- L89 — `bool ReadToggleState(void* toggle)`
  note: reads kButtonToggleStateOffset & 1
- L93 — `void DumpControlVtable(void* control, char* out, size_t outSize)`
  note: dumps vtable[0/4/20/22]; used to correlate unknown controls back to specific classes via SARIF
- L99 — `void* ResolveItemFromClientHandle(uint32_t clientHandle)`
  note: AppManager → CServerExoApp ClientToServer + GetItemByGameObjectID; SEH per hop
- L103 — `void* ResolveItemFromServerHandle(uint32_t serverHandle)`
  note: skips ClientToServer translation; for already-server-side handles from CSWInventory
- L108 — `bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize)`
  note: calls CSWSItem::GetPropertyDescription; deliberately leaks heap c_string to avoid CRT mismatch
- L117 — `int ReadItemRowStackCount(void* rowControl)`
  note: returns stack_size>1 for stackable, 1 for single (caller stays silent), 0 for non-item-row or infinite-stock store items
- L122 — `bool IsInventoryItemRow(void* control)`
  note: vtable-identity check for CSWGuiInGameItemEntry only; excludes store rows
