# engine_radial.cpp (937 lines)

Implementation of engine_radial.h. Re-states several offset/read helpers
locally (ReadGuiStringLocal, ReadCExoStringLocal) rather than depending on
engine_picker or menus.cpp, to keep the engine layer dependency-clean.

## Declarations (in source order)

- L20 — `namespace { ... }` (anonymous, TU-local offsets)
- L24 — resolve-chain offsets, `kMainInterfaceTargetActionMenuOffset` = 0xBC
- L34 — action_lists / target_actions stride constants
- L48 — per-row embedded control offsets (action_button, action_label, up/down_button, field4/5, is_action@+0x718)
- L59 — CSWGuiInterfaceAction stride/offsets (label/id/target/icon)
- L67 — `kTamNameLabelOffset` = 0x15CC, `kTamField1Offset` = 0x24, `kTamTargetTypeOffset` = 0x1AEA
- L82 — Engine entry addresses: SelectNextAction @0x006865b0, SelectPrevAction @0x00686680, DoTargetAction @0x00689610, GetGameObject @0x005ED580, GetCanUseSkill @0x006477E0
- L101 — `constexpr size_t kCreatureLvlUpStatsOffset = 0x2F8`
- L107 — `constexpr size_t kServerDoorSecurityGateOffset = 0x2D8`
- L113 — GameObjectMethods vtable indices: AsSWCDoor(0x14), AsSWCCreature(0x28), AsSWCTrigger(0x38), AsSWCPlaceable(0x48)
- L131 — CSWCDoor field offsets: cannot_bash(0x104), can_use_actions(0x108), is_hostile(0x114), state(0x11c), field17(0x138)
- L144 — `typedef void (__thiscall* PFN_RowOp)(void*, int)`
- L149 — `typedef void (__thiscall* PFN_DoTargetAction)(void*, int, int)`
  note: DoTargetAction purges 8 bytes (ret 8) vs 4 for Select ops — needs a matching pad param or it corrupts the caller's frame.
- L154 — `void* GetClientExoApp()`
- L166 — `void* GetClientExoAppInternal(void* exoApp)`
- L177 — `void* GetGuiInGame(void* internal)`
- L188 — `void* GetMainInterface(void* guiInGame)`
- L199 — `bool ReadInt32(void* base, size_t offset, int32_t* out)`
- L214 — `bool ReadGuiStringLocal(void* control, size_t guiStringPtrOffset, char* outBuf, size_t bufSize)`
- L243 — `bool ReadCExoStringLocal(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L265 — `void* RowActionAddr(void* tam, int row)`, `RowActionButtonAddr(void* tam, int row)`
- L279 — `namespace acc::engine_radial`
- L281 — `void* ResolveTargetActionMenu()`
- L291 — `int RowActionCount(void* tam, int row)`
- L301 — `namespace { void* FindSelectedActionDescriptor(void* tam, int row) }`
  note: reads field1[target_type*3+row] to find the selected action_id; -1 or no-match falls back to data[0], mirroring the engine's own SelectNextAction lookup loop.
- L346 — `uint32_t ReadSelectedRowActionId(void* tam, int row)`
- L359 — `bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize)`
  note: three-path fallback — gui_string, then inline CExoString on the button, then FindSelectedActionDescriptor's label — needed because the rendered button text lags one paint pass behind PopulateMenus (verified in patch-20260505-101621.log).
- L392 — `bool ReadTargetName(void* tam, char* outBuf, size_t bufSize)`
- L405 — `void LogState(void* tam, const char* tag)`
  note: hex-dumps first 0x40 bytes, parsed action_lists[0..2], per-row is_action + gui_string label, and name_label.
- L466 — `namespace { void ReadResRefLocal(...), void ReadButtonText(...) }`
- L505 — `void LogStateWide(void* tam, const char* tag)`
  note: LogState + field1[12] dump + per-row all-4-embedded-button text + action_lists[r].data[0] peek for capacity-but-unset-size recovery.
- L587 — `bool SelectNextActionInRow(void* tam, int row)`
- L599 — `bool SelectPrevActionInRow(void* tam, int row)`
- L611 — `bool DispatchRowAction(void* tam, int row)`
- L623 — `bool SelectActionInRow(void* tam, int row, int index)`
- L653 — `uint32_t ReadRowActionIdAtIndex(void* tam, int row, int index)`
- L670 — `int FindRowIndexByActionId(void* tam, int row, uint32_t actionId)`
- L691 — `int RetargetRowActions(void* tam, int row, uint32_t targetClientHandle)`
  note: overwrites creature_id on every entry in the row, logging the stale-target transition when it differed.
- L721 — `void* GetRowActionButton(void* tam, int row)`
- L732 — `namespace { void* CallVtableAsClass(void* gameObject, size_t vtableOffset) }`
- L755 — `void LogTargetDiag(uint32_t targetClient, const char* tag)`
  note: resolves + downcasts to door/creature/placeable/trigger, dumps door precondition fields + server-side Security gate + leader Security-skill check, and annotates which precondition would fail.
- L919 — `bool IsCreatureClientTarget(uint32_t handle)`
