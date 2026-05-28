# menus.cpp (2663 lines)

Main menu-accessibility TU. Contains the two primary engine hook entry points (`OnSetActiveControl`, `OnHandleInputEvent`), focus-chain helpers, public namespace functions, and additional diagnostic hooks. Refactor steps 1-5 split large monitors and dispatch tables to sibling TUs; this file retains the core hook glue and public coordination functions.

## Declarations (in source order)

- L173 — `namespace acc::menus` — block containing `SpeakIfChanged` and `MarkSpoken`
- L179 — `void acc::menus::MarkSpoken(int channel, const char* text)`
- L184 — `void acc::menus::SpeakIfChanged(int channel, const char* text)` — dedup across 4 speech channels; calls prism::Speak only when text differs from last-spoken on that channel
- L239 — `void* g_currentPanel = nullptr` — file-scope global; set in OnSetActiveControl
- L259 — `static bool g_drilledIntoSubScreen = false`
- L316 — `static void* g_lastTitledPanel = nullptr`
- L332 — `bool acc::menus::detail::GetControlCenter(void* control, int& outCx, int& outCy)`
- L352 — `static bool GetListBoxRowScreenCenter(void* lb, void* row, int& outCx, int& outCy)` — computes screen center of a listbox row for cursor-warp
- L377 — `bool acc::menus::detail::IsChainNavigable(void* control)` — true for Button and Toggle vtables
- L399 — `static void AnnouncePanelTitle(void* panel)` — resolves and speaks the panel title via spec overrides, then PanelKindName
- L521 — `void* acc::menus::detail::FindControlById(void* panel, int id)` — scans panel.controls for control with .gui ID == id
- L569 — `bool acc::menus::detail::IsSaveLoadPanel(void* panel)` — structural matcher checking for SaveLoad-specific control IDs
- L605 — `const char* acc::menus::detail::ReadSaveLoadEntryString(void* entry, size_t fieldOffset)` — reads a CExoString from a saveload row struct at the given field offset
- L627 — `bool acc::menus::detail::DriveListBoxSelection(void* listbox, ListBoxNavOp op, short minSel, ListBoxNavResult& out)` — moves listbox cursor (Up/Down/Home/End) and returns old/new indices and row pointer
- L703 — `bool acc::menus::detail::QueueButtonByIdActivate(void* panel, int buttonId, const char* logPrefix)` — finds a button by .gui ID and queues a FireActivate for it
- L726 — `bool acc::menus::detail::IsClassSelectionIcon(void* panel, void* control)` — true when control is one of the 6 class-icon buttons in CSWGuiClassSelection
- L759 — `const char* acc::menus::detail::ClassLabelCacheLookup(void* panel, void* icon)` — returns cached class label for (panel, icon) key or nullptr
- L769 — `void acc::menus::detail::ClassLabelCacheStore(void* panel, void* icon, const char* text)` — stores class label; first-write-wins per (panel, icon) key
- L841 — `static void WalkChildren(const char* label, void* parent, size_t offset)` — diagnostic traversal of a panel's controls array
- L907 — `static void PrefillClassIconCacheOnTransition(void* panel, void* newControl)` — pre-populates the class-icon cache when focus arrives on a CSWGuiClassSelection panel
- L938 — `static void UpdateFocusedPanelState(void* panel)` — resets cycle-category cache and captures chargen panel labels on panel transition
- L949 — `static void WalkAndCaptureOnFirstSight(void* panel)` — performs a one-time walk of panel controls to capture cycle categories and class icons
- L1017 — `static void SpeakPanelTitleOnFirstSight(void* panel)` — calls AnnouncePanelTitle the first time each panel becomes foreground
- L1031 — `static void AnnounceNewFocusedControl(int n, void* panel, void* newControl)` — resolves and announces the newly-focused control; handles chargen special cases and mod-settings virtual entry
- L1089 — `extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl)` — entry hook (CSWGuiPanel::SetActiveControl); fires once per navigation event; drives chain rebind, title speech, focus announcement
- L1152 — `extern "C" void __cdecl OnListBoxSetActiveControl(void* listBox, void* newRow, int param2)` — entry hook for listbox row focus; drives listbox-specific announce path
- L1301 — `extern "C" void __cdecl OnHandleFocusChange(void* thisPtr, int param_1)` — log-only hook; correlates with input events in log
- L1324 — `extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2)` — main keyboard-input hook; dispatches to listbox/editbox/chargen/powers/modsettings/chain handlers; handles Esc, Home/End, Left/Right cycle and slider, returns 1 to consume or 0 to pass through
- L2373 — `void* acc::menus::detail::FindListBoxChild(void* panel)` — first-match scan for a CSWGuiListBox child
- L2407 — `namespace acc::menus`
- L2407 — `void acc::menus::ValidatePanels()` — calls ValidateTabbedPanel + ValidateChainPanel each tick
- L2426 — `void acc::menus::TickMonitors()` — fans out to store, general, listbox, editbox monitors in order
- L2445 — `void acc::menus::PollHomeEndKeys()` — polls hotkeys and synthesises OnHandleInputEvent calls for Home/End
- L2481 — `void acc::menus::TickPendingOps()` — resolves GuiManager singleton and calls pending::Drain
- L2505 — `void acc::menus::DrainPendingAnnounce()` — flushes pending-announce slot; chain-coherence guard drops engine sibling-focus echos
- L2550 — `void acc::menus::ClearPendingAnnounce()`
- L2555 — `bool acc::menus::IsDrilledIntoSubScreen()`
- L2556 — `void acc::menus::SetDrilledIntoSubScreen(bool drilled)`
- L2564 — `static void DumpListBoxState(void* listBox, char* out, size_t outSize)` — shared log helper for listbox hooks
- L2582 — `extern "C" void __cdecl OnListBoxLMouseDown(void* listBox)` — diagnostic hook; logs click-press state
- L2595 — `extern "C" void __cdecl OnListBoxLMouseUp(void* listBox)` — diagnostic hook; logs click-release state
- L2610 — `extern "C" void __cdecl OnListBoxHandleInput(void* listBox)` — diagnostic hook; logs per-listbox key dispatch
- L2623 — `extern "C" void __cdecl OnListBoxSetSelectedControl(void* listBox)` — diagnostic hook; logs selection-index change pre-update
- L2647 — `extern "C" void __cdecl OnSetMoveToModuleString(void* serverApp, void* arg_addr)` — pre-load area-transition hook; calls transitions::AnnouncePreLoadDestination; workaround for LEA-vs-MOV bug in stack-param wrapper
