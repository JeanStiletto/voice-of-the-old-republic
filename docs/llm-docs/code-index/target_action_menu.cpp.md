# target_action_menu.cpp (294 lines)

Implementation of the target-action submenu. Maintains per-row shadow indices that survive PopulateMenus action_id reassignment; mutex with actionbar_menu; re-stamps field1 on both Open and Enter to prevent stale-variant fire.

## Declarations (in source order)

- L22 — `namespace acc::target_action_menu`
- L24 — `namespace { // anonymous`
- L26 — `constexpr uint32_t kInvalidObjectId = 0x7F000000u;`
- L34 — `int g_selectedIndex[acc::engine_radial::kRowCount] = {0, 0, 0};`
  note: per-row shadow index; persists across submenu sessions and PopulateMenus rebuilds; re-derives action_id from live descriptor on every stamp
- L36 — `struct State`
- L40 — `State g_state;`
- L43 — `int ClampIndex(void* tam, int row)`
  note: clamps g_selectedIndex[row] to live RowActionCount before use
- L57 — `uint32_t ResolveNarratedClientHandle()`
  note: converts server-side narrated_target handle to client-side (OR 0x80000000) for PrepareBareDispatch; returns kInvalidObjectId for map pins
- L73 — `void SpeakCurrentLabel(void* tam, int row)`
- L84 — `} // namespace (anonymous)`
- L86 — `int CurrentSelection(int row)`
- L91 — `bool Open(int row)`
- L170 — `bool IsActive()`
- L174 — `bool HandleInputEvent(int code, int value)`
  note: Shift+arrow speaks tooltip without cycling; Up/Down updates shadow index then stamps field1 via SelectActionInRow; Enter re-stamps before firing to handle no-cycle Open→Enter case
- L287 — `void ForceDisarm(const char* reason)`
