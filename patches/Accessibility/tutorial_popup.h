// On-demand tutorial popup for the voice-acted Endar-Spire tutorial lines
// (Trask / end_pop*). Those lines stay suppressed during their Basic VO; when
// the dialogue reaches a reply prompt (the game's own break), we mount a REAL
// engine tutorial popup carrying the keyboard hint, so it pauses and dismisses
// exactly like a stock game tutorial.
//
// Mechanism (see scratchpad trask_popup_recipe.md / docs):
//   * Clear the tutorial once-shown bit for a fixed reason, then call
//     CGuiInGame::ShowTutorialWindow directly (bypasses the funnel's
//     "no tutorials during dialogue" gate) to mount the box.
//   * Set the box's visible text to the line's original strref (mouse wording)
//     via CSWGuiMessageBox::SetMessage(strref) — mirrors the Surface-1 split.
//   * Route the SPOKEN text to our keyboard hint via the synthetic flag below
//     (the existing TutorialBox reader in menus_monitors honours it).

#pragma once

#include <cstdint>

namespace acc::tutorial_popup {

// Remember the keyboard hint (and its source strref, for the popup's visible
// text) for the most recent rewritten tutorial dialogue line. Fired later at
// the next reply-prompt break.
void RecordPendingHint(uint32_t strref, const char* hint);

// Called when a dialogue reply becomes navigable. If a hint is pending, mount
// the tutorial popup carrying it and clear the pending hint. Returns true if it
// fired (so the caller can skip its own announce), false otherwise.
bool FirePendingAtReplyBreak();

// True while our synthetic tutorial popup is on screen — the TutorialBox speech
// paths (fingerprint override, listbox suppression, chain gate) route the hint
// and keep the mouse-worded message off the arrow-nav chain.
bool SyntheticActive();
const char* SyntheticHint();

// Per-tick: detect the synthetic popup closing (Weiter/OK) and clear state +
// unpause. Cheap; safe to call every frame.
void PollDismiss();

}  // namespace acc::tutorial_popup
