# menus_chain.cpp (847 lines)

Chain navigation implementation TU. The main `RebindChain` function (~520 lines) walks all panel controls, inserts ChainEntry records sorted by (cy, cx), handles tabs clusters, equip slots, virtual entries (mod-settings sentinel, credits rows, stat rows, equip-stat rows), and deduplicates by control pointer.

## Declarations (in source order)

- L44 — `acc::menus::chain::ChainEntry g_chain[kMaxChainEntries]` — exported chain array
- L45 — `void* g_chainPanel` — panel the chain is bound to
- L46 — `int g_chainIndex` — currently focused chain slot
- L47 — `int g_chainCount` — number of valid entries in g_chain
- L48 — `void* g_tabbedPanel` — the enclosing tabbed panel (Options-style), or nullptr
- L49 — `int g_tabsStart`, `g_tabsCount` — range within g_chain of tab buttons
- L50 — `int g_tabClickOffsetY` — y-pixel compensation for Options-style hit-test shift
- L51 — `int g_equipSlotClickOffsetY` — y-pixel compensation for equip-slot hit-test shift
- L52 — `int g_classIconClickOffsetX` — x-pixel compensation for class-icon hit-test
- L60 — `bool IsModalTextPanel(PanelKind k)` — returns true for TutorialBox, BarkBubble, AreaTransition, MessageBoxModal (anonymous ns)
- L76 — `void* acc::menus::chain::ReadPanelActiveControl(void* panel)` — reads panel.active_control at +0x4c
- L82 — `int acc::menus::chain::FindChainEntry(void* control)` — linear scan of g_chain; returns index or -1
- L94 — `bool acc::menus::chain::DetectTabsCluster(void* panel, int& outStart, int& outCount)` — locates contiguous tab-button run in g_chain for Options-style tabbed panels
- L140 — `void acc::menus::chain::ResetTabbedState()`
- L146 — `void acc::menus::chain::RebindChainPreserveIndex(void* panel)` — rebuilds chain and restores g_chainIndex to the nearest valid slot
- L158 — `void acc::menus::chain::InvalidateChain()` — zeroes g_chainCount and nulls g_chainPanel; call before a panel is freed to prevent stale-pointer dereferences in monitors
- L165 — `void acc::menus::chain::ValidateTabbedPanel()` — clears g_tabbedPanel if the panel is no longer in the manager
- L183 — `void acc::menus::chain::ValidateChainPanel()` — calls InvalidateChain if g_chainPanel is no longer in the manager
- L205 — `bool acc::menus::chain::IsTabButton(void* control)` — true when control is in the g_chain tabs cluster
- L217 — `void* acc::menus::chain::FindAdjacentArrow(void* panel, void* focused, bool toRight)` — finds the cycle-widget flanker arrow spatially adjacent to `focused` on the same row
- L254 — `void* acc::menus::chain::FindCloseButton(void* panel)` — scans controls for a Schliess/OK/Weiter button by .gui ID or text
- L276 — `void* acc::menus::chain::FindCancelButton(void* panel)` — scans controls for an Abbrechen/Cancel button by .gui ID or text
- L299 — `void AppendChainEntry(void* control)` — inserts sorted by (cy, cx) into g_chain (anonymous ns)
- L307 — `void AppendChainTextOnly(void* control, void* panel)` — inserts a text-only entry for controls that announce but aren't click-targets (anonymous ns)
- L326 — `void acc::menus::chain::RebindChain(void* panel)` — full rebuild; iterates panel.controls, virtual rows (credits, stat rows, equip-stat rows, mod-settings sentinel), filters non-navigable and hidden controls, sorts, detects tabs cluster
