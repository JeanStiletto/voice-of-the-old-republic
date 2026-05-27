# menus_editbox.cpp (580 lines)

Editbox accessibility implementation. Arms on entering an editbox-hosting panel, polls the engine's append-only c_string for character diffs, and handles modal keys (Esc/Enter/Up/Down).

## Declarations (in source order)

- L44 — `struct EditboxPanelSpec` — fields: matches callback, findEditbox, findSubmitButton, titleOverride callback (anonymous ns)
- L78 — `bool ChargenNameMatches(void* panel)` (anonymous ns)
- L84 — `void* ChargenNameFindEditbox(void* panel)` (anonymous ns)
- L89 — `void* ChargenNameFindSubmitButton(void* panel)` (anonymous ns)
- L99 — `const char* ChargenNameTitleOverride(void* panel)` (anonymous ns)
- L110 — `constexpr EditboxPanelSpec kChargenNameSpec` (anonymous ns)
- L118 — `constexpr const EditboxPanelSpec* kSpecs[]` — one entry: chargen-name panel (anonymous ns)
- L127 — `struct ArmedState` — captures editbox pointer, text snapshot, lengths, and panel pointer for diff polling (anonymous ns)
- L144 — `ArmedState s_state` — the single armed-state slot (anonymous ns)
- L186 — `bool ReadEditbox(void* editbox, char* outText, size_t outCap, uint32_t& outLen, uint32_t& outRawLen, short& outA, short& outB)` — SEH-guarded read of the editbox's c_string and length fields (anonymous ns)
- L209 — `void SnapshotInto(ArmedState& s)` — captures current editbox text into ArmedState (anonymous ns)
- L218 — `void SpeakFullText(const char* text, uint32_t len)` — speaks the complete current text (anonymous ns)
- L230 — `void SpeakSingleChar(char c)` — speaks a single typed character (anonymous ns)
- L239 — `void PollAndAnnounceDiff(ArmedState& s)` — compares current vs. snapshot and speaks additions or deletions (anonymous ns)
- L308 — `void PollModalKeys(ArmedState& s)` — polls Up/Down hotkeys and re-reads full text on press (anonymous ns)
- L370 — `struct PanelMatch` (anonymous ns)
- L375 — `PanelMatch FindMatchingPanel()` — scans manager panels[] against kSpecs; returns matched spec + panel (anonymous ns)
- L398 — `void DisarmIfArmed(const char* reason)` — clears s_state and logs the reason (anonymous ns)
- L411 — `bool acc::menus::editbox::TryHandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)`
- L476 — `void acc::menus::editbox::TickEditboxMonitors()`
- L569 — `const char* acc::menus::editbox::GetTitleOverride(void* panel)`
