# engine_input.cpp (236 lines)

Engine keyboard-code name lookup, KOTOR's internal logical-action-code translation table, and the three DirectInput acquire/release primitives (EnsureInputAcquired / ForceReacquireInput / ReleaseInput) that fix cold-start and post-load keyboard-death bugs, plus a coalescing deferred-drain mechanism for focus-event callers (diag_focus.cpp) that can't safely call SetActive re-entrantly from inside a wndproc. Talks to log, engine_rebase.

## Declarations (in source order)

- L17 — `constexpr uintptr_t kAddrExoInputGlobal = 0x007a39e4` — engine's CExoInput facade global (SYMBOL ExoInput)
- L23 — `const uintptr_t kAddrCExoInputSetActive = R(0x005df540)` — CExoInput::SetActive(this, active)
- L25 — `typedef void(__thiscall* PFN_CExoInputSetActive)(void*, int)`
- L32 — `const char* InputIndexName(int code)` (public)
  note: static 132-entry table lifted from Lane's InputIndices enum SARIF; -1→"INPUTDEVICE_NONE", 0xCE→"LOGICAL_TAB" special-cased
- L113 — `int ManagerTranslateCode(int code)` (public)
  note: mirrors CSWGuiManager::HandleInputEvent's inline switch (decompiled @0x0040c8e0): 0xb4/0xdf→F2(cancel), 0xb5/0xbb→F1(confirm), 0xb6-0xb9→nav-up/down/next/prev (user-configurable, can't be named generically)
- L125 — `bool EnsureInputAcquired()` (public)
  note: replicates HideLoadScreen's activate call byte-for-byte (SetActive(1)); idempotent by engine design
- L147 — `bool ForceReacquireInput()` (public)
  note: drives a real SetActive(0)->SetActive(1) edge — SetActive(1) alone no-ops when the active flag is stuck at 1 with DirectInput actually unacquired
- L173 — `bool ReleaseInput()` (public)
  note: SetActive(0) on focus-loss so keyboard input mirrors foreground (engine acquires at background cooperative level by default, which bleeds nav keys into the game while windowed and unfocused)
- L210 — `std::atomic<int> g_pendingInputState` — 0=none, 1=acquire, 2=release; last-writer-wins
- L213/217 — `void RequestInputReacquire()`, `void RequestInputRelease()` (public)
- L221 — `void DrainPendingReacquire()` (public) — call once per tick on the engine thread
