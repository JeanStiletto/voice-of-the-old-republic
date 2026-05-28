# engine_reads.cpp (441 lines)

Implementation of engine_reads.h. No leading comment block.

## Declarations (in source order)

- L10 — `namespace acc::engine`
- L12 — `bool ReadControlNameFields(void* control, const char*& outTip, uint32_t& outTipLen, int& outId)`
  note: direct field reads at +0x28/+0x2c/+0x50; no SEH — callers are responsible for validating control pointer
- L22 — `void* CallDowncast(void* control, int vtableIndex)`
- L35 — `bool ReadCExoString(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L46 — `uint32_t ReadU32(void* base, size_t offset)`
- L67 — `bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize)`
  note: deliberately leaks CExoString.c_string from engine allocation (CRT-mismatch risk); validated sanity bounds before invoking engine; __try/__except to contain exception unwinds that previously disabled hooks by unwinding through trampolines
- L91 — `bool ExtractTextOrStrRef(void* control, size_t cexoOffset, size_t strRefOffset, char* outBuf, size_t bufSize)`
- L99 — `bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize)`
  note: 8-hop parent-walk with cycle guard; strref priority over literal per engine decompile; SEH-guards each field read
- L158 — `bool ReadGuiString(void* control, size_t guiStringPtrOffset, char* outBuf, size_t bufSize)`
  note: vtable-checks against kVtableCAurGUIStringInternal before deref; avoids /GS fastfail on garbage pointer in chargen Class buttons
- L185 — `bool ExtractTextOrStrRefIndirect(void* control, size_t cexoOffset, size_t strRefOffset, size_t textObjectOffset, char* outBuf, size_t bufSize)`
  note: derives guiStringPtrOffset from cexoOffset-4 (layout identity — gui_string ptr is always 4 bytes before inline CExoString)
- L219 — `bool IsToggle(void* control)`
- L232 — `bool IsSlider(void* control)`
  note: vtable-identity via kVtableSlider; SEH-guarded after 2026-05-11 crash on freed PartySelection OK button during status=4 teardown window
- L242 — `bool IsListBox(void* control)`
- L252 — `bool IsEditbox(void* control)`
- L262 — `bool ReadToggleState(void* toggle)`
- L266 — `void DumpControlVtable(void* control, char* out, size_t outSize)`
- L277 — `void* ResolveItemFromClientHandle(uint32_t clientHandle)`
  note: AppManager → kAppManagerServerExoAppOffset → ClientToServerObjectId → GetItemByGameObjectID; SEH per hop
- L323 — `void* ResolveItemFromServerHandle(uint32_t serverHandle)`
  note: same chain minus ClientToServer step
- L357 — `namespace { ReadItemStackSize, IsItemEntryRow }` (anonymous)
- L363 — `int ReadItemStackSize(void* item)`
  note: reads kSwsItemBitFlagsOffset and kSwsItemStackSizeOffset; returns 0 on infinite-stock bit set
- L379 — `bool IsItemEntryRow(void* control)`
  note: vtable matches kVtableCSWGuiInGameItemEntry OR kVtableCSWGuiStoreItemEntry
- L394 — `int ReadItemRowStackCount(void* rowControl)`
  note: gates on IsItemEntryRow, reads client handle at kStoreItemEntryObjIdOffset, resolves via ResolveItemFromClientHandle + ReadItemStackSize
- L409 — `bool IsInventoryItemRow(void* control)`
- L420 — `bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize)`
