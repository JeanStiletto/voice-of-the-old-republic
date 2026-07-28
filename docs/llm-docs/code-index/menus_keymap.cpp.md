# menus_keymap.cpp (430 lines)

Accessibility layer for the ENGINE's Tastaturbelegung/Key-Mapping screen (CSWGuiInGameOptKeyMappings, optkeymapping.gui) — distinct from menus_keybinds (the mod's own configurator). Two-level dispatcher mirroring menus_abilities: tab level ([Bewegung, Spiel, Minigames, OK, Abbrechen, Standard] flat list) then list level (binding rows). Maintains its OWN authoritative row cursor (`s_rowCursor`) because the engine rewrites the listbox's `selection_index` between keypresses via hover-select, which otherwise pins Up/Down to the two rows flanking one stuck index; drives the list via `DriveListBoxSelectionEngine` (real SetSelectedControl) and parks the OS cursor off the list (`ParkCursorToCorner`, deferred to `Tick()` since MoveMouseToPosition recurses through the hover pipeline). After a capture completes, warns if the newly-bound game key also drives a mod hotkey (`WarnIfModBindClash`). Talks to `engine_keymap` (InputIndexToVk, IsKeyUsedByGame), `hotkeys` (FindConflict), `menus_chain` (RebindChain after category switch), `menus_extract` (FromControl row readout), `menus_keybinds` (DisplayName for clash naming), `menus_pending` (QueueActivate for OK/Cancel/Default).

## Declarations (in source order)

- L37-40 — `const uintptr_t kAddrSetCaptureEvent/OnFilterMove/OnFilterGame/OnFilterMini` — engine entry points, decompiled from build/re/optkeymappings-keymapbutton
- L43 — `constexpr size_t kCaptureActiveOff = 0xf2c`
- L47-53 — `constexpr int kIdListBox/DefaultBtn/AcceptBtn/CancelBtn/FilterMove/FilterGame/FilterMini` — optkeymapping.gui stable IDs
- L58 — `enum class EntryKind { Category, Button }`; `struct TabEntry { kind, guiId, filterFn }`
- L64 — `const TabEntry kTabEntries[6]` — 3 category tabs + Accept/Cancel/Default
- L76-102 — state: `s_drilled, s_panel, s_tabCursor, s_armedRow, s_armedPanel, s_parkPending, s_rowCursor`
- L104 — `void* FindByGuiId(panel, wantId)` (anonymous ns)
- L123 — `void* RowAt(panel, index)`
- L145 — `void SetSelectionIndex(lb, v)` — forces listbox selection_index back to our cursor before each engine-driven step
- L153 — `int CaptureActive(panel)`
- L163 — `void SpeakControl(panel, ctrl)` — via FromControl
- L172 — `void AnnounceTabEntry(panel, idx)`
- L181 — `bool StepRow(panel, ListBoxNavOp op)` — drives DriveListBoxSelectionEngine, announces landed row
- L207 — `void SwitchCategory(panel, filterFn)` — calls OnFilter{Move,Game,Mini}
  note: OnFilter* is an AddEvent(0x27) click handler with BYTES_PURGED=4 (ret 4) despite Ghidra's `(this)`-only signature — the typedef must carry the dummy 4-byte arg or the callee corrupts the caller's frame
- L216 — `void ArmCapture(panel, row)` — calls SetCaptureEvent
- L230 — `void WarnIfModBindClash(void* row)` — after the engine applies a bare key to a game action, checks it against mod hotkeys via `hotkeys::FindConflict`
- L257 — `bool acc::menus::keymap::IsKeyMapPanel(void* panel)`
- L263 — `void acc::menus::keymap::Tick()` — issues the deferred cursor park; on capture-completion re-announces the row + WarnIfModBindClash
- L294 — `bool acc::menus::keymap::HandleInput(void* activePanel, param_1, param_2, int& outRv)` — tab-level nav/drill/button-activate; list-level nav/Enter-arm-capture/Esc-undrill; hands every key to the engine while a capture is armed
