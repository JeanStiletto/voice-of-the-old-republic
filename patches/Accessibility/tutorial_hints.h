// Endar Spire / global tutorial keyboard-hint substitution.
//
// The vanilla KOTOR tutorial is worded for mouse. Two surfaces carry it:
//   * Silent tutorial.2da popups (TutorialBox) — game-wide.
//   * Voice-acted Trask / "pop" dialogue on the Endar Spire.
//
// We do NOT touch the on-screen text (a rest-sight player who reads the
// screen still gets the mouse wording). We substitute a keyboard-oriented
// line into the SPOKEN channel only:
//   * Surface 1: menus_monitors reads the popup's tutorial row (+0x994) and,
//     for a mapped row, speaks HintForTutorialRow() instead of the rendered
//     mouse text.
//   * Surface 2: dialog_speech matches the rendered line against the engine's
//     TLK resolution of a known mouse-line strref and speaks HintForDialogLine().
//
// Both paths are pure polls — no engine hooks, no dialog.tlk edits, no installer
// change. See docs and scratchpad tutorial inventory for the full mapping.

#pragma once

#include <cstdint>

namespace acc::tutorial_hints {

// Read the tutorial.2da row index off a live TutorialBox panel
// (CSWGuiTutorialBox +0x994, uint8 — RE-confirmed). Returns -1 if the panel
// pointer is null or the read faults.
int ReadTutorialRow(void* tutorialBoxPanel);

// Keyboard-hint replacement for a silent tutorial popup, by row index.
// Returns nullptr for rows with no mouse wording (leave the vanilla text).
const char* HintForTutorialRow(int row);

// Keyboard-hint replacement for a rendered dialogue line whose source strref
// is a known mouse-oriented tutorial line. Returns nullptr when the line is
// not one we rewrite. Language-independent: the target strrefs are resolved
// through the engine's own TLK the first time this is called. When a match is
// found and outStrref is non-null, it receives the line's source strref (used
// as the on-demand popup's visible text).
const char* HintForDialogLine(const char* renderedLine, uint32_t* outStrref = nullptr);

// If `text` is the raw (mouse-worded) message of a mapped tutorial popup — the
// engine TLK resolution of one of the mapped rows' message strrefs — return the
// row's keyboard hint; else nullptr. Matched on text (not panel identity) so it
// works before the popup registers in panels[] and can't touch an unrelated
// modal (quit-confirm, save prompt). Used to substitute the hint when the user
// arrow-navigates onto the popup's message in the chain.
const char* HintForMouseText(const char* text);

// Convenience predicate over HintForMouseText — used to suppress the single-row
// message-listbox monitor on the popup's first-sight so the content-fingerprint
// keyboard hint is the sole speaker there.
bool IsSuppressedTutorialText(const char* text);

}  // namespace acc::tutorial_hints
