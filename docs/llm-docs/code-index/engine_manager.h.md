# engine_manager.h (83 lines)

CSWGuiManager surface — singleton lookup, panels[]/modal_stack walks, cursor + click-sim primitives. Pure read + raw PFN typedefs. File-scope constants (not namespaced) for callsite brevity.

## Declarations (in source order)

- L15 — `namespace acc::engine`
- L22 — `void* FindOwningPanel(void* control)`
  note: fallback for callers without explicit owner; scans panels[] children up to 256 per panel
- L28 — `bool IsPanelInManager(void* panel)`
  note: pointer-equality scan only, no deref of panel — safe with stale pointers
- L32 — `void* GetForegroundPanel(void* mgr)`
  note: modal_stack top if non-empty, else last panels[] entry skipping render-only kinds (Fade)
- L36 — `void LogManagerStack(void* mgr, const char* tag)`
- L41 — `constexpr uintptr_t kAddrGuiManagerPtr = 0x007A39F4`
- L49 — `constexpr size_t kMgrPanelsDataOffset     = 0x88`
- L50 — `constexpr size_t kMgrPanelsSizeOffset     = 0x8c`
- L51 — `constexpr size_t kMgrModalStackDataOffset = 0x94`
- L52 — `constexpr size_t kMgrModalStackSizeOffset = 0x98`
- L57 — `constexpr uintptr_t kAddrMoveMouseToPosition = 0x0040c790`
- L58 — `typedef void (__thiscall* PFN_MoveMouseToPosition)(void*, int, int)`
- L79 — `constexpr uintptr_t kAddrManagerLMouseDown = 0x0040c570`
- L80 — `constexpr uintptr_t kAddrManagerLMouseUp   = 0x0040a170`
- L81 — `typedef int (__thiscall* PFN_ManagerLMouseDown)(void*, int)`
- L82 — `typedef int (__thiscall* PFN_ManagerLMouseUp)(void*)`
