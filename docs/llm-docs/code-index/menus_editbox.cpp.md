# menus_editbox.cpp (639 lines)

Editbox dispatcher + per-tick monitor. Single armed-editbox model (`s_state`) since vanilla KOTOR has exactly one editbox at a time, mirrored across two specs: chargen `CSWGuiNameChargen` and the save-name popup `CSWGuiSaveNamePanel`. Engine consumes keystrokes at the editbox before the manager-level input hook sees them, so text changes are caught by a per-tick strnlen-based diff (`ReadEditbox`/`PollAndAnnounceDiff`) rather than by handling letters/Backspace directly; modal keys (Up/Down re-read, Enter submit, Esc exit) are polled via Win32 (`PollModalKeys`) because they're likewise swallowed by the engine. Talks to `menus_extract` (FromControl for title overrides), `hotkeys` (edge-detect registry), `input_pipeline` (NoteEditboxSubmitClosed to prevent a false in-world interact), `menus_pending`/`menus.h`.

## Declarations (in source order)

- L46 — `struct EditboxPanelSpec { logTag, matches, findEditbox, findSubmitButton, titleOverride }` — one entry per panel kind hosting an editbox
- L80 — `bool ChargenNameMatches/FindEditbox/FindSubmitButton/TitleOverride(void*)` — CSWGuiNameChargen spec callbacks; title override reads subtitle_label instead of the stale main_title_label
- L124 — `bool SaveNameMatches/FindEditbox/FindSubmitButton/TitleOverride(void*)` — CSWGuiSaveNamePanel spec callbacks
- L162 — `constexpr EditboxPanelSpec* kSpecs[]` — the two specs
- L172 — `struct ArmedState { spec, panel, editbox, editMode, text[64], textLen, rawLenField, shortA, shortB }`
  note: textLen is strnlen(c_string), NOT the engine's +0x15c field — verified the raw field desyncs on Backspace
- L189 — `ArmedState s_state` — file-static single-slot arm state
- L231 — `bool ReadEditbox(editbox, outText, outCap, outLen, outRawLen, outA, outB)` — reads (text, length, shortA/B) from the editbox
- L254 — `void SnapshotInto(ArmedState&)`
- L263 — `void SpeakFullText(text, len)` / L275 `void SpeakSingleChar(c)`
- L284 — `void PollAndAnnounceDiff(ArmedState&)` — diffs text vs last snapshot; ±1-char delta speaks the single inserted/deleted char (assumes append-at-end, no caret tracked), else speaks full new text
- L353 — `void PollModalKeys(ArmedState&)` — Win32 edge-detect Up/Down (re-read), Enter (drop edit mode only — engine handles submit natively; double-QueueActivate caused a modal-stack lockup, see comment), Esc (soft exit)
- L434 — `struct PanelMatch { spec, panel }`; `PanelMatch FindMatchingPanel()` — walks manager panels[] for a spec match
- L457 — `void DisarmIfArmed(reason)`
- L470 — `bool acc::menus::editbox::TryHandleInput(n, thisPtr, activePanel, param_1, param_2, outRv)` — consumes Esc/Enter/Up/Down/Home/End while armed+editMode; lets letters/Backspace/Left/Right fall to the engine
- L535 — `void acc::menus::editbox::TickEditboxMonitors()` — arms on focus-enter (speaks "Editbox. <value>", consumes held-key edges via hotkeys::Consume), disarms on focus-leave or panel gone, else runs PollAndAnnounceDiff + PollModalKeys
- L628 — `const char* acc::menus::editbox::GetTitleOverride(void* panel)` — dispatches to the matching spec's titleOverride
