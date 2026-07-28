# dialog_speech.h (25 lines)

Public surface for live dialog-screen narration (poll-based; hook points identified but not yet wired: SetDialogMessage@0x6a7010, SetReplies@0x6a86a0, SetBark@0x6a9920).

## Declarations (in source order)

- L22 — `void Tick()`
  note: idle when no dialog panel mounted; NPC line/reply-count/bark all edge-speak on change, per-row nav via existing ListBoxPanelSpec plumbing
