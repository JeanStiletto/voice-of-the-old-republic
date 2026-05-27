# engine_radial.cpp (832 lines)

Implementation of engine_radial.h. No leading comment block.

## Declarations (in source order)

- L273 — `namespace acc::engine_radial`
- L275 — `void* ResolveTargetActionMenu()`
  note: walks GetClientExoApp → GetClientExoAppInternal → GetGuiInGame → GetMainInterface, returns pointer into mainIf at kMainInterfaceTargetActionMenuOffset; borrowed
- L285 — `int RowActionCount(void* tam, int row)`
  note: reads action_lists[row].size; range-checks row and size before returning
- L295 — `namespace { FindSelectedActionDescriptor }` (anonymous)
  note: reads field1[target_type*3+row] to find the currently-selected action_id; falls back to data[0] on -1 or no-match; mirrors engine's own SelectNextAction fallback
- L302 — `void* FindSelectedActionDescriptor(void* tam, int row)`
- L340 — `bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize)`
  note: three-path: ReadGuiStringLocal on action_button → ReadCExoStringLocal on button text → FindSelectedActionDescriptor + ReadCExoStringLocal on kIfActionLabelOffset; source-of-truth fallback required because rendered button text lags populate-time (verified in patch log)
- L373 — `bool ReadTargetName(void* tam, char* outBuf, size_t bufSize)`
  note: reads name_label at kTamNameLabelOffset via gui_string then inline CExoString
- L386 — `void LogState(void* tam, const char* tag)`
  note: hex-dumps first 0x40 bytes of TAM, logs action_lists[0..2] data/size/cap, target_actions[0..2] is_action + gui_string label, and name_label
- L447 — `namespace { ReadResRefLocal, ReadButtonText }` (anonymous helpers for LogStateWide)
- L486 — `void LogStateWide(void* tam, const char* tag)`
  note: calls LogState first, then dumps field1[12], per-row action_button/action_label/up_button/down_button + field4/field5, and action_lists[r].data[0] peek for capacity-but-size=0 recovery
- L568 — `bool SelectNextActionInRow(void* tam, int row)`
- L580 — `bool SelectPrevActionInRow(void* tam, int row)`
- L592 — `bool DispatchRowAction(void* tam, int row)`
- L604 — `bool SelectActionInRow(void* tam, int row, int index)`
  note: reads action_lists[row].data[index].action_id, stamps field1[target_type*3+row]; rejects zero action_id or target_type >= 4
- L634 — `void* GetRowActionButton(void* tam, int row)`
  note: returns tam + kTamTargetActionsOffset + row * kTargetActionStride + kRowActionButtonOffset
- L645 — `namespace { CallVtableAsClass }` (anonymous)
  note: calls vtable[vtableOffset] as __thiscall on gameObject; SEH-guarded
- L668 — `void LogTargetDiag(uint32_t targetClient, const char* tag)`
  note: resolves target via GetGameObject, downcasts to creature/door/placeable/item, logs per-class fields GetTargetActions checks; annotates which precondition would block Security/Bash rows
