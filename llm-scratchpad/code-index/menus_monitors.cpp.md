# menus_monitors.cpp (594 lines)

General per-tick monitors TU. Contains the focused-control monitor, content-fingerprint monitor, dialog-reply selection monitor, and in-game sub-screen tracking.

## Declarations (in source order)

- L49 — `void acc::menus::monitors::AnnounceControl(void* control)` — speaks control text now; also writes channel-0 dedup state and monitor last-text to prevent immediate re-fire
- L75 — `void MonitorFocusedControl()` — per-tick; reads the chain's focused control and re-extracts its text; speaks when text changes (anonymous ns)
- L152 — `struct InGameSubScreenSpec` — fields: PanelKind, strref, literal fallback (anonymous ns)
- L159 — `const InGameSubScreenSpec k_inGameSubScreens[8]` — Equipment, Inventory, Character, Map, Abilities, Journal, Options, Messages (anonymous ns)
- L172 — `const InGameSubScreenSpec* FindSpec(PanelKind k)` (anonymous ns)
- L179 — `void* s_visibleSubScreens[16]`, `int s_visibleSubScreenCount` — rolling list of tracked sub-screen panel pointers (anonymous ns)
- L182 — `bool IsSubScreenTracked(void* p)` (anonymous ns)
- L189 — `void AnnounceNewSubScreens(void** panels, int count)` — detects newly-visible in-game sub-screens and speaks their name; auto-arms drill flag (anonymous ns)
- L247 — `struct ContentSnapshot` — (panel, fingerprint text[512]) entry (anonymous ns)
- L251 — `constexpr int kMaxContentSnapshots = 8`
- L252 — `ContentSnapshot s_contentSnapshots[kMaxContentSnapshots]` (anonymous ns)
- L255 — `bool IsContentMonitored(PanelKind k)` — returns true for ~15 content-bearing panel kinds (anonymous ns)
- L279 — `void BuildContentFingerprint(void* panel, char* out, size_t outSize)` — concatenates non-button label texts from panel.controls into a " | "-delimited fingerprint string (anonymous ns)
- L325 — `bool FingerprintContainsSegment(const char* hay, size_t hayLen, const char* seg, size_t segLen)` — delimiter-aware substring test (anonymous ns)
- L345 — `void SpeakNewSegments(const char* prev, const char* curr)` — splits curr into " | " segments and speaks each one absent from prev (anonymous ns)
- L367 — `char* GetContentSnapshot(void* panel)` — returns existing snapshot text buffer or inserts a new entry (LRU eviction when full) (anonymous ns)
- L384 — `void MonitorPanelContents()` — per-tick; calls AnnounceNewSubScreens then fingerprints each monitored panel, speaks new segments on change (anonymous ns)
- L441 — `struct DialogReplyState` + `DialogReplyState s_dialogReplyState` (anonymous ns)
- L447 — `bool IsDialogPanelKind(PanelKind k)` (anonymous ns)
- L459 — `void MonitorDialogReplies()` — per-tick; arms on dialog panel entry, speaks row text on selection-index change (anonymous ns)
- L551 — `void acc::menus::monitors::TickGeneralMonitors()` — DrainPendingAnnounce → MonitorFocusedControl → MonitorPanelContents → MonitorDialogReplies → SyncSelectedAbilityFromChainFocus → SyncSelectedSkillFromChainFocus
- L573 — `void* acc::menus::monitors::FindActiveSubScreenPanel()`
- L590 — `bool acc::menus::monitors::IsInGameSubScreenKind(PanelKind k)`
