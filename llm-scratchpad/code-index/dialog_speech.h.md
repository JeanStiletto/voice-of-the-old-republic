# dialog_speech.h (24 lines)

Live dialog screen narration — poll-based. Covers NPC line (CSWGuiDialog.message_label @+0x1ca4), reply count, Computer dialog terminal lines, and BarkBubble. Per-row reply nav handled by menus_listbox ListBoxPanelSpec plumbing.

## Declarations (in source order)

- L19 — `namespace acc::dialog_speech`
- L22 — `void Tick()`
  note: idle when no dialog panel is mounted; gates on IsModuleLoadPending
