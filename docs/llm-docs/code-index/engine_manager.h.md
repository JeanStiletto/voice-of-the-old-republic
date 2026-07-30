# engine_manager.h (83 lines)

CSWGuiManager surface — singleton lookup, panels[]/modal_stack walks,
cursor + click-sim primitives. Pure read + raw PFN typedefs. File-scope
constants (not namespaced) for callsite brevity, matching engine_input.h's
convention.

**The panel-array seam lives here** (Phase-3 B3b). Before it, reading the
manager's `panels[]` / `modal_stack` was written out eighteen times across
eight files and EIGHT of those copies had no SEH guard — including two in
`engine_manager.cpp` sitting directly above `GetForegroundPanel`, whose own
comment documents the exact teardown hazard. Use `ReadPanelArray` /
`ReadModalStack`; do not hand-roll the walk.

## Declarations (in source order)

- `void* GetGuiManager()` — `*kAddrGuiManagerPtr`, SEH-guarded.
- `int ReadPanelArray(void* mgr, void** out, int maxEntries, int* outRawCount = nullptr)`
- `int ReadModalStack(void* mgr, void** out, int maxEntries, int* outRawCount = nullptr)`
  note: COPIES into the caller's buffer inside the guard, so callers iterate
  their own memory and a torn-down array cannot fault them mid-loop. Entries
  can still be stale engine pointers — guard what you do with them.
  note: `maxEntries` is the caller's sanity bound against a corrupt size
  field, not an engine limit. **Every site passes 32.** The pre-B3b copies
  disagreed (most 16, four 32), so two of our own queries could contradict
  each other about the same panel whenever more than 16 were live — rare, but
  `patch-20260530-112606.log` recorded panels.size climbing to 27 with
  modal.size 24 inside one second. Unified on 32 in the same commit.
- L22 — `void* FindOwningPanel(void* control)`
  note: fallback when caller doesn't pass owner explicitly; scans panels[] children up to 256 per panel (raised from 32 — CSWGuiInGameCharacter alone has 60+ children).
- L29 — `bool IsPanelInManager(void* panel)`
  note: pointer-equality scan only, no deref of panel — safe with stale/wild pointers.
- L33 — `void* GetForegroundPanel(void* mgr)`
  note: modal_stack top if non-empty, else last panels[] entry (last-pushed draws on top), else nullptr.
  note: deliberately NOT on ReadPanelArray — its last-resort return indexes `panelData[panelSize - 1]` with the RAW size while its scan covers only the first 32, so the two disagree above 32 and the fallback reads outside its own scan window. Open finding, not fixed here.
- L37 — `void LogManagerStack(void* mgr, const char* tag)`
  note: doubles as live verification of the SARIF-derived modal_stack offset.
- L42 — `constexpr uintptr_t kAddrGuiManagerPtr = 0x007A39F4`
- L50 — `constexpr size_t kMgrPanelsDataOffset      = 0x88`
- L51 — `constexpr size_t kMgrPanelsSizeOffset      = 0x8c`
- L52 — `constexpr size_t kMgrModalStackDataOffset  = 0x94`
- L53 — `constexpr size_t kMgrModalStackSizeOffset  = 0x98`
- L58 — `const uintptr_t kAddrMoveMouseToPosition = R(0x0040c790)`
- L59 — `typedef void (__thiscall* PFN_MoveMouseToPosition)(void*, int, int)`
- L80 — `const uintptr_t kAddrManagerLMouseDown = R(0x0040c570)`
- L81 — `const uintptr_t kAddrManagerLMouseUp = R(0x0040a170)`
  note: click-sim primitives — calling press then release in sequence after MoveMouseToPosition runs the engine's natural click pipeline (replaces the SetActiveControl path that crashed at mgr+5).
- L82 — `typedef int (__thiscall* PFN_ManagerLMouseDown)(void*, int)`
- L83 — `typedef int (__thiscall* PFN_ManagerLMouseUp)(void*)`
