# engine_radial.h (105 lines)

Engine bindings for CSWGuiTargetActionMenu (KOTOR's radial). Pure read + primitives. The radial is embedded in CSWGuiMainInterface at +0xBC, not a top-level panel. Documents PopulateMenus wrapper/inner split, TAM layout (+0x00 action_lists[3], +0x24 field1[12], +0x54 target_actions[3], +0x15CC name_label, +0x1AEA target_type), and all engine entry points.

## Declarations (in source order)

- L54 — `namespace acc::engine_radial`
- L56 — `constexpr int kRowCount = 3`
- L58 — `void* ResolveTargetActionMenu()`
  note: borrowed pointer; re-resolve each tick
- L63 — `int RowActionCount(void* tam, int row)`
  note: action_lists[row].size; max-across-rows == 0 is the "menu gone" signal
- L67 — `bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize)`
  note: tries gui_string → inline CExoString → action_lists[row].data[selected] descriptor; last path covers the empty-button window immediately after PopulateMenus
- L71 — `bool ReadTargetName(void* tam, char* outBuf, size_t bufSize)`
- L74 — `void LogState(void* tam, const char* tag)`
- L79 — `void LogStateWide(void* tam, const char* tag)`
  note: LogState + field1[12] + all embedded buttons + data[0] peek; one-shot deep diagnostic
- L84 — `bool SelectNextActionInRow(void* tam, int row)`
- L85 — `bool SelectPrevActionInRow(void* tam, int row)`
- L86 — `bool DispatchRowAction(void* tam, int row)`
- L93 — `bool SelectActionInRow(void* tam, int row, int index)`
  note: stamps field1[target_type*3+row] = action_lists[row].data[index].action_id; DoTargetAction falls back to data[0] on miss
- L97 — `void* GetRowActionButton(void* tam, int row)`
  note: target_actions[row].action_button; coincides with array entry start (action_button is field 0)
- L103 — `void LogTargetDiag(uint32_t targetClient, const char* tag)`
  note: resolves target via GetGameObject, downcasts, logs per-class fields GetTargetActions checks; annotates which precondition would block Security/Bash rows
