# engine_radial.h (149 lines)

Engine bindings for CSWGuiTargetActionMenu (KOTOR's radial). Pure read +
primitives. The radial is embedded in CSWGuiMainInterface at +0xBC, not a
top-level panel. Documents the PopulateMenus wrapper/inner split, TAM
layout (+0x00 action_lists[3], +0x24 field1[12] selected-action-id per
target_type×row, +0x54 target_actions[3], +0x15AC main_interface back-ptr,
+0x15CC name_label, +0x1AEA target_type), and all engine entry points
(SelectNextAction/SelectPrevAction/DoTargetAction).

## Declarations (in source order)

- L54 — `namespace acc::engine_radial`
- L56 — `constexpr int kRowCount = 3`
- L58 — `void* ResolveTargetActionMenu()`
  note: borrowed pointer; re-resolve each tick.
- L63 — `int RowActionCount(void* tam, int row)`
  note: action_lists[row].size; max-across-rows==0 is the "menu gone" signal.
- L67 — `bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize)`
- L71 — `bool ReadTargetName(void* tam, char* outBuf, size_t bufSize)`
- L74 — `void LogState(void* tam, const char* tag)`
- L79 — `void LogStateWide(void* tam, const char* tag)`
  note: LogState + field1[12] + per-row embedded buttons + action_lists[r].data[0] peek even when size==0.
- L84 — `bool SelectNextActionInRow(void* tam, int row)`
- L85 — `bool SelectPrevActionInRow(void* tam, int row)`
- L86 — `bool DispatchRowAction(void* tam, int row)`
- L93 — `bool SelectActionInRow(void* tam, int row, int index)`
  note: stamps field1[target_type*3+row] = action_lists[row].data[index].action_id; mirrors engine_actionbar::SelectVariant so submenus keep a shadow index across PopulateMenus rebuilds.
- L101 — `uint32_t ReadRowActionIdAtIndex(void* tam, int row, int index)`
  note: by POSITION, unlike ReadSelectedRowActionId which uses engine field1 tracking; paired with FindRowIndexByActionId to carry a selection across a rebuild by identity.
- L105 — `int FindRowIndexByActionId(void* tam, int row, uint32_t actionId)`
- L118 — `int RetargetRowActions(void* tam, int row, uint32_t targetClientHandle)`
  note: overwrites creature_id on EVERY descriptor in action_lists[row] — the engine re-bakes lists against its current cursor target while the unified menu sits open, so a dispatch seconds after arming would otherwise fire at the wrong object (Machtbruch-on-Malak bug). Raw field write only, safe from the poll context where RePopulate is not.
- L122 — `void* GetRowActionButton(void* tam, int row)`
- L132 — `uint32_t ReadSelectedRowActionId(void* tam, int row)`
  note: resolves via field1[target_type*3+row], falls back to data[0]; same descriptor the item-variant Shift+arrow peek uses.
- L138 — `void LogTargetDiag(uint32_t targetClient, const char* tag)`
  note: resolves target via GetGameObject, downcasts, logs per-class fields GetTargetActions checks (door: cannot_bash/can_use_actions/is_hostile/state + server-side Security gate + leader's Security skill).
- L147 — `bool IsCreatureClientTarget(uint32_t handle)`
  note: used to decide whether the 3 target rows are the named Attacks/Force-Powers/Items categories (creature) or per-object actions (door/placeable/trigger).
