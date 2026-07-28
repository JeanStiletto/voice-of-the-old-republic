# menus_monitors.cpp (950 lines)

Owns the three "general" per-tick monitors that lifted out of menus.cpp: `MonitorFocusedControl` (re-extracts the focused chain entry's text each tick, speaks the diff — catches toggle/cycle/slider state changes not driven by our own key handlers), `MonitorPanelContents` (per-panel content-fingerprint diff for content-monitored panel kinds: TutorialBox, MessageBoxModal, AreaTransition, StatusSummary, InGameMap, InGameJournal, InGameCharacter — dialog/combat-log/container panels are explicitly excluded to avoid duplicate speech with their own listbox specs), and `MonitorDialogReplies` (polls the FIXED-offset reply listbox at +0x19c4 since the engine mutates its selection_index via hover/arrow-key without firing SetActiveControl; sole speaker for dialogue replies). Also hosts `ParkCursorToCorner` (the shared cursor-park primitive every keyboard-driven listbox screen uses to defeat the engine's per-frame hover-reselect, parked at (24,2) — clear of the camera edge-turn band that caused an earlier endless-spin regression) and the sub-screen tracking table (`InGameSubScreenSpec`, `AnnounceNewSubScreens` — sole site that auto-arms the sub-screen drill state). Talks to `menus_extract` (FromControl), `menus_chain` (RebindChain/RebindChainPreserveIndex), `menus_charsheet`/`menus_chargen_attr`/`menus_chargen_skills` (override hooks), `tutorial_hints`/`tutorial_popup` (keyboard-hint substitution), `menus_journal` (LogEntryCounts diagnostic).

## Declarations (in source order)

- L77-78 — `constexpr int kCursorParkX = 24, kCursorParkY = 2` (anonymous ns)
- L81 — `bool acc::menus::detail::ParkCursorToCorner(const char* tag)` — defined here though declared in menus_internal.h; must run from Update tick only
- L102-104 — `void* s_focusMonitorControl`, `char s_focusMonitorText[256]` — shared state with AnnounceControl
- L106 — `void acc::menus::monitors::AnnounceControl(void* control)` — multi-row-listbox-container guard (auto-focus on open shouldn't speak "control N"), tutorial-hint substitution, falls back to "control %d" placeholder
- L191 — `void MonitorFocusedControl()` (anonymous ns) — eager rebind for CSWGuiPortraitCharGen (opens without firing OnSetActiveControl); bypasses the g_currentPanel gate for that vtable; delegates value-change re-announce override to chargen_attr/chargen_skills AnnounceValueChange
- L307 — `struct InGameSubScreenSpec { kind, guiId, strref, literal }`
- L314 — `const InGameSubScreenSpec k_inGameSubScreens[9]` — Equip/Inventory/Character/Map/Abilities/Journal/Options/Messages/QuestItems
- L331 — `const InGameSubScreenSpec* FindSpec(PanelKind k)`
- L338 — `void* s_visibleSubScreens[16]`, `int s_visibleSubScreenCount`
- L341 — `bool IsSubScreenTracked(void* p)`
- L348 — `void AnnounceNewSubScreens(void** panels, int count)` — reads InGameQuestItems' LBL_TITLE live (no static strref); sole site that auto-arms `SetDrilledIntoSubScreen(true)`
- L443 — `struct ContentSnapshot { panel, text[8192] }`; `constexpr int kMaxContentSnapshots = 8`; `s_contentSnapshots[]`, `s_contentSnapshotCount`
- L451 — `bool IsContentMonitored(PanelKind k)` — allowlist of panel kinds (see summary)
- L499 — `void BuildContentFingerprint(panel, out, outSize)` — concatenates non-button/toggle control text with " | " separators; StatusSummary filters to visible-bit rows only
- L554 — `bool FingerprintContainsSegment(hay, hayLen, seg, segLen)`
- L574 — `void SpeakNewSegments(prev, curr)` — speaks only " | "-delimited segments new to curr
- L596 — `char* GetContentSnapshot(void* panel)` — ring-buffer eviction at kMaxContentSnapshots
- L613 — `void MonitorPanelContents()` — TutorialBox fingerprint override via tutorial hint lookup; first-sight suppression for Container/sub-screen-spec panels (deferred to the kind-name announce path); rebinds chain if content changed while it owns focus (character-sheet Force-points row appearing/disappearing on Tab)
- L703 — `struct DialogReplyState { listBox, lastSelection }`; `s_dialogReplyState`
- L711 — `bool s_dialogReplyParkPending`
- L713 — `bool ParkDialogCursorOffReplies(void* replyLb)` — logs reply-list geometry, calls ParkCursorToCorner
- L723 — `bool IsDialogPanelKind(PanelKind k)` — the 4 dialogue variants
- L735 — `void MonitorDialogReplies()` — reads the FIXED +0x19c4 listbox (NOT FindListBoxChild — a UI mod renumbering LB_MESSAGE below LB_REPLIES would return the wrong listbox); prefers `ReadDialogReplyText` (render-independent, catches off-page replies) over the row label; defers to `tutorial_popup::FirePendingAtReplyBreak` before speaking
- L905 — `void acc::menus::monitors::TickGeneralMonitors()` — drains pending-announce, runs the 3 monitors, polls tutorial-popup dismiss, re-syncs chargen selected_ability/selected_skill against chain focus (engine cursor-warp race)
- L929 — `void* acc::menus::monitors::FindActiveSubScreenPanel()`
- L946 — `bool acc::menus::monitors::IsInGameSubScreenKind(PanelKind k)`
