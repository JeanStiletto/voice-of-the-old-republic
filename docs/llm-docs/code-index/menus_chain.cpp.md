# menus_chain.cpp (1828 lines)

Implements the chain-navigation state and its four input handlers
(`HandleNavStep`, `HandleEnterActivation`, `HandleLeftRight`, `HandleEsc`) plus
`RebindChain` — the ~700-line heart of the module that walks panel.controls,
recurses into sub-dialog listboxes, inserts virtual entries (credits row,
InGameCharacter/StatusSummary/Pazaak-wager/Equip-stat rows, mod-settings
sentinel), filters per-kind decorative controls, sorts by y, squashes
cycle-arrow flankers, and computes the three click-offset compensations.
`HandleEnterActivation` classifies the focused entry (virtual/tab/equip-slot/
workbench-slot/store-item/journal-row/quest-item/party-add-blocked/
level-up-inactive-step/wager-step/text-only/default) and queues the matching
pending op. Talks to nearly every menus_* module for per-panel special
cases.

## Declarations (in source order)

- L48 — `namespace acc::menus::chain`
- L54-64 — `ChainEntry g_chain[]` / `g_chainPanel` / `g_chainIndex` / `g_chainCount` / `g_tabbedPanel` / `g_tabsStart` / `g_tabsCount` / `g_equipSlotClickOffsetY` / `g_classIconClickOffsetX` (definitions)
- L66 — `int ComputeTabClickOffsetY(void* panel)`
- L84 — `bool IsModalTextPanel(PanelKind k)` (anonymous ns)
  note: MessageBoxModal/TutorialBox/AreaTransition only — StatusSummary handled separately (label cluster, not listbox)
- L107 — `bool IsPanelLive(void* panel)` (anonymous ns)
  note: guards stale g_currentPanel against a freed-and-reused heap block reading back as garbage/ASCII (crash dump swkotor.exe(1).31228.dmp)
- L127 — `void* ReadPanelActiveControl(void* panel)`
- L133 — `int FindChainEntry(void* control)`
- L148 — `void SpeakLevelUpDoStepFirst()`
  note: finds the level-up category with bit_flags bit 3 set (the engine's "current step" gate) and names it
- L180 — `bool DetectTabsCluster(void* panel, int& outStart, int& outCount)`
  note: excludes Store (action buttons, not tabs) and WorkbenchUpgrade (slot pickers, not tabs) explicitly
- L246 — `void ResetTabbedState()`
- L252 — `void RebindChainPreserveIndex(void* panel)`
- L264 — `void InvalidateChain()`
  note: deliberately does NOT reset tabbed-panel state — orthogonal lifetimes
- L278 — `void ValidateTabbedPanel()`
- L286 — `void ValidateChainPanel()`
- L298 — `bool IsTabButton(void* control)`
- L310 — `void* FindAdjacentArrow(void* panel, void* focused, bool toRight)`
- L347 — `void* FindCloseButton(void* panel)`
  note: matches "Schliess"/"Close"/"OK"/"Weiter"/"Continue" text prefixes
- L369 — `void* FindCancelButton(void* panel)`
  note: matches "Abbrechen"/"Cancel"/"Nein"/"No" — probed before FindCloseButton at Esc call sites
- L392 — `void AppendChainEntry(void* control)` (anonymous ns)
- L400 — `void AppendChainTextOnly(void* control, void* panel)` (anonymous ns)
- L419 — `void RebindChain(void* panel)`
  note: per-kind decorative filter (isDecorative lambda) drops InGameCharacter model-rotate button, InGameEquip OK/Back/party-cycle, InGameLevelUp Zurück/Abbrechen, PartySelection unavailable portraits, WorkbenchUpgrade inactive slots, and the universal strref-1582 close-button match; virtual-row registration (credits, InGameCharacter stats, StatusSummary, Pazaak wager, Equip stats, mod-settings root) happens before the y-sort
- L1102 — `void HandleEnterActivation(void* activePanel, int code, int val, bool& consumed)`
  note: classification order — virtual entry, tab button, equip slot (direct OnEnterSlot+OnSelectSlot bypass, NOT click-sim — labels cover buttons in z-order), workbench-upgrade slot (same z-order trap), store item row, journal row, quest-item row, party-add-blocked, level-up inactive step, Pazaak wager step, else generic FireActivate
- L1397 — `void WalkChildren(const char* label, void* parent, size_t offset, const char* kindName)`
  note: uses acclog::BlockLog with pointer-stripped Key() so repeated walks of a recreated panel fold to "(repeated Nx)"
- L1457 — `void HandleNavStep(void* activePanel, int code, int val, bool& consumed)`
  note: silences in-flight speech before each step on the chargen Skills panel (long descriptions otherwise queue one step behind); applies tab/class-icon/attr/skill cursor-warp Y compensations
- L1616 — `void HandleLeftRight(void* activePanel, int code, int val, bool& consumed)`
  note: Pazaak wager popup owns Left/Right itself; sliders route to QueueSliderInput; else cycle-arrow neighbour (with a CSWGuiPortraitCharGen direct-offset override since right_arrow is filtered from the chain)
- L1693 — `void HandleEsc(void* activePanel, int code, int val, bool& consumed)`
  note: Store and WorkbenchUpgrade get dedicated direct-button routes (neither is a modal popup nor tabbed); generic path probes FindCancelButton before FindCloseButton so confirm dialogs route to the safe choice; InvalidateChain() called for InGameOptions sub-screen closes (deferred-destroy crash guard, dump TID 16116)
