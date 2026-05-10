// Live dialog screen narration — Phase 1D.
//
// Layer: poll-based event narration. The plan identifies hook points
// (CSWGuiDialog::SetDialogMessage @0x6a7010,
// CSWGuiDialog::SetReplies @0x6a86a0,
// CSWGuiBarkBubble::SetBark @0x6a9920); for the initial skeleton we
// poll the panel state per tick and edge-detect changes. Switching to
// hooks later is a one-line wiring change.
//
// Coverage:
//
//   * NPC line — `CSWGuiDialog.message_label @+0x1ca4` text. On change
//     speak the new line. Same applies to `DialogCinematicCopy` and
//     `DialogComputer*` variants (all share base layout offsets per
//     swkotor.exe.h).
//
//   * Reply count — `CSWGuiDialog.replies_listbox @+0x19c4` row count.
//     On grow from 0 to N (engine populates per-node), speak "N replies
//     available" so the user knows the panel transitioned to a
//     reply-presentation node.
//
//   * Bark bubble — `CSWGuiBarkBubble`. The bark is held inside the
//     panel via `SetBark @0x6a9920`. We poll the panel's text content;
//     on change speak the new bark line.
//
// Per-row reply navigation + per-row "(unavailable)" suffix is layered
// in via the existing ListBoxPanelSpec plumbing in `menus_listbox.cpp`
// (registered alongside the in-game messages spec in this same Phase).

#pragma once

namespace acc::dialog_speech {

// Per-tick poll. Walks the manager's panels[] for active dialog panels;
// on NPC-line change speak it, on replies appearing speak the count cue.
// Idle when no dialog panel is mounted.
void Tick();

}  // namespace acc::dialog_speech
