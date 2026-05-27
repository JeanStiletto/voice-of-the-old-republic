// editbox (text-input field) input dispatcher + monitor.
//
// One vanilla user: the chargen Name screen's `name_editbox` (the only
// CSWGuiEditbox in the game). The TU is shaped as a spec-table mirror of
// menus_listbox.{h,cpp} so adding a future editbox (mod-introduced) is one
// new spec value, not another copy of the dispatch+monitor scaffolding.
//
// Edit-mode model (patch-internal, NOT an engine state):
//
//   * Auto-enter when chain focus lands on the editbox. The focus-enter
//     speech ("Eingabefeld. <text>") is produced by FromControl in
//     menus_extract.cpp (step 6b) — same pattern as slider / cycle / label
//     extractions. The per-tick monitor here only initialises the diff
//     snapshot on this edge; it does not speak.
//   * In edit mode, this dispatcher consumes:
//       - Up / Down → re-speak the full current text (single-line field, so
//         vertical motion has nothing else to do).
//       - Enter     → fire the panel's submit button (BTN_OK / "Annehmen")
//         via the deferred-op queue and exit edit mode. No confirmation
//         speech — the next panel's own announce takes over.
//       - Esc       → silently exit edit mode. The editbox keeps engine
//         focus; subsequent Up/Down then fall through to chain nav so the
//         user can move to OK / Random / Abbrechen.
//   * Letter / Backspace / Left / Right are NOT consumed — the engine
//     handles caret movement and text mutation natively. The per-tick poll
//     diffs the (text, caret) snapshot each frame and announces:
//       - length grew by 1 → speak the inserted char (caret-1)
//       - length shrunk by 1 → speak the deleted char (from prior snapshot)
//       - |Δlength| > 1 → speak the full new text (covers Zufallsname
//         button replacing the field in one shot)
//       - caret moved without length change → speak char at new caret;
//         "Ende" if the caret is past the last char.
//
// Why focus-enter speech lives in extract (not here): keeps the per-control
// announce path uniform — chain nav's AnnounceControl always asks
// FromControl, and any control-class with composed speech (slider,
// cycle widgets, this editbox) plugs in there. The monitor is then a thin
// "diffs after the entry announce" layer.

#pragma once

namespace acc::menus::editbox {

// Try to handle the input event against the active editbox spec. Returns
// true if a spec was in edit mode and decided to consume the event —
// `outRv` carries the value to return (always 1 on consumed). Returns
// false if no spec matches the active panel, the matched spec isn't in
// edit mode, or it didn't claim this particular key (e.g. letters,
// Backspace, Left/Right — those need to flow to the engine's editbox
// handler).
//
// Called from menus.cpp's OnHandleInputEvent after the listbox dispatcher
// and before chain navigation, so an in-edit-mode Up/Down re-read fires
// before chain nav would otherwise consume the same key to step focus.
bool TryHandleInput(int n, void* thisPtr, void* activePanel,
                    int param_1, int param_2, int& outRv);

// Per-tick monitor. Walks the manager's panels[] for any spec-matching
// panel; on focus-edge transitions onto the editbox arms edit mode +
// snapshots state; while armed, polls (text, caret) and announces deltas.
// Called from menus.cpp's TickMonitors alongside TickGeneralMonitors and
// TickListboxMonitors.
void TickEditboxMonitors();

// Title-speech override lookup. If `panel` matches an editbox spec that
// supplies its own title text (e.g. CSWGuiNameChargen carries a stale
// "CHARAKTERAUSWAHL" main_title_label that the chargen flow doesn't
// overwrite — the screen-specific title lives on subtitle_label), returns
// the localised replacement string. Returns nullptr if no spec matches or
// the matched spec has no override.
//
// Called from menus.cpp's AnnouncePanelTitle alongside the listbox
// equivalent. Lifting the hook into the spec table keeps panel-specific
// title knowledge co-located with the rest of the panel's spec.
const char* GetTitleOverride(void* panel);

}  // namespace acc::menus::editbox
