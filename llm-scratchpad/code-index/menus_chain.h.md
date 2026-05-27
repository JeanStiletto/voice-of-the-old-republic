# menus_chain.h (149 lines)

Public surface for chain navigation (the flat sorted list of focusable controls walked by arrow keys). Declares `namespace acc::menus::chain`.

## Declarations (in source order)

- L1 — `namespace acc::menus::chain`
- L10 — `struct ChainEntry` — fields: `void* control`, `int cx`, `int cy`, `bool textOnly`, `int virtualKind`
- L20 — `constexpr int kVirtualMod_SettingsRoot = 1` — sentinel value for the mod-settings virtual chain entry
- L22 — `constexpr int kMaxChainEntries = 64`
- L25 — `extern ChainEntry g_chain[]`
- L26 — `extern void* g_chainPanel`
- L27 — `extern int g_chainIndex`
- L28 — `extern int g_chainCount`
- L30 — `extern void* g_tabbedPanel`
- L31 — `extern int g_tabsStart`
- L32 — `extern int g_tabsCount`
- L34 — `extern int g_tabClickOffsetY`
- L35 — `extern int g_equipSlotClickOffsetY`
- L36 — `extern int g_classIconClickOffsetX`
- L40 — `void RebindChain(void* panel)` — full rebuild of g_chain from panel's controls
- L41 — `void RebindChainPreserveIndex(void* panel)` — rebuild preserving g_chainIndex
- L42 — `void InvalidateChain()` — clears g_chain state; call before panel is freed
- L44 — `void ResetTabbedState()`
- L45 — `void ValidateTabbedPanel()`
- L46 — `void ValidateChainPanel()`
- L48 — `bool DetectTabsCluster(void* panel, int& outStart, int& outCount)`
- L49 — `bool IsTabButton(void* control)`
- L51 — `void* FindAdjacentArrow(void* panel, void* focused, bool toRight)` — locates cycle-widget flanker arrow for Left/Right dispatch
- L52 — `void* FindCloseButton(void* panel)`
- L53 — `void* FindCancelButton(void* panel)`
- L55 — `int FindChainEntry(void* control)` — returns index in g_chain or -1
- L56 — `void* ReadPanelActiveControl(void* panel)`
