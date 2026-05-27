// Live dialog screen narration — poll-based.
//
// Plan identifies hook points (SetDialogMessage @0x6a7010, SetReplies
// @0x6a86a0, SetBark @0x6a9920); polling is the first cut. Switching to
// hooks is a one-line wiring change.
//
// Coverage:
//   - NPC line — CSWGuiDialog.message_label @+0x1ca4. Edge-speak on change.
//     Same offsets apply to DialogCinematicCopy and DialogComputer*.
//   - Reply count — CSWGuiDialog.replies_listbox @+0x19c4. On grow 0→N
//     speak "N replies available".
//   - Bark bubble — CSWGuiBarkBubble text. Edge-speak on change.
//
// Per-row reply nav + "(unavailable)" suffix come via the existing
// ListBoxPanelSpec plumbing in menus_listbox.cpp.

#pragma once

namespace acc::dialog_speech {

// Idle when no dialog panel is mounted.
void Tick();

}  // namespace acc::dialog_speech
