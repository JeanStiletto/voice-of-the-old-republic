# menus_chain.h (212 lines)

Declares the chain-navigation abstraction: a flat, visually-sorted list of
focusable controls on the current panel that arrow keys walk top-to-bottom.
Owns the chain array/cursor state, tab-cluster detection, and the three
click-offset compensations (tab-y, equip-slot-y, class-icon-x) needed because
cursor-warp hit-testing on several panels resolves one row/column off from
the intended target. Speech utilities and modal/focus state stay in
menus.cpp; this header is the seam between the two.

## Declarations (in source order)

- L18 — `namespace acc::menus::chain`
- L21 — `struct ChainEntry { void* control; int cx, cy; bool textOnly; int virtualKind; }`
  note: textOnly = non-activatable body text (modal popups); virtualKind non-zero = sentinel entry (e.g. mod-settings root), not a real engine control
- L40 — `constexpr int kVirtualMod_SettingsRoot = 1`
- L50 — `constexpr int kMaxChainEntries = 512`
  note: sized for a hoarder's full Inventory/Store listbox; rebind is O(n²) but runs only on panel-open/content-change, not per tick
- L55-58 — `extern ChainEntry g_chain[]` / `g_chainPanel` / `g_chainIndex` / `g_chainCount`
- L64-66 — `extern void* g_tabbedPanel` / `int g_tabsStart` / `g_tabsCount`
- L78-79 — `extern int g_equipSlotClickOffsetY` / `g_classIconClickOffsetX`
- L87 — `int ComputeTabClickOffsetY(void* panel)`
  note: computed on demand (not at rebind time) to dodge a rebind-before-detect race
- L94 — `void RebindChain(void* panel)`
- L106 — `void RebindChainPreserveIndex(void* panel)`
  note: used after in-place listbox repopulate (e.g. Store sell/buy) so cursor position survives a row's removal
- L119 — `void InvalidateChain()`
  note: must be called before the engine frees a panel's children — dangling g_chain entries otherwise crash (dump swkotor.exe.18312.dmp)
- L123 — `void ResetTabbedState()`
- L127 — `void ValidateTabbedPanel()`
- L138 — `void ValidateChainPanel()`
  note: guards against a dangling g_chainPanel after commit-style buttons synchronously destroy their parent panel (dump swkotor.exe(1).31052.dmp)
- L144 — `bool DetectTabsCluster(void* panel, int& outStart, int& outCount)`
- L148 — `bool IsTabButton(void* control)`
- L153 — `void* FindAdjacentArrow(void* panel, void* focused, bool toRight)`
- L160-161 — `void* FindCloseButton(void* panel)` / `void* FindCancelButton(void* panel)`
  note: cancel-first probe order routes Esc on confirm dialogs to the safe option
- L164 — `int FindChainEntry(void* control)`
- L168 — `void* ReadPanelActiveControl(void* panel)`
- L179 — `void WalkChildren(const char* label, void* parent, size_t offset, const char* kindName = nullptr)`
- L186 — `void HandleEsc(void* activePanel, int code, int val, bool& consumed)`
- L192 — `void HandleLeftRight(void* activePanel, int code, int val, bool& consumed)`
- L199 — `void HandleNavStep(void* activePanel, int code, int val, bool& consumed)`
- L209 — `void HandleEnterActivation(void* activePanel, int code, int val, bool& consumed)`
