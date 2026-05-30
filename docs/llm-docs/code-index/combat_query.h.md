# combat_query.h

Read-only queries over creature stats and the examine panel. No engine re-entry beyond
documented accessors. Each entry self-gates on player loaded.

Surfaces: leader-change auto-announce; cycle/passive-narrate enrichment for
Creature-kind targets; Shift+H Examine; bare-H self-status.

## Declarations (in source order)

- `namespace acc::combat::query`
- `void TickLeaderChangeAutoAnnounce()`
  note: polls active leader name; speaks name only on change
- `bool BuildTargetCombatBrief(void* targetServerObject, const char* targetName, char* outBuf, size_t outBufSize)`
  note: appends condition/distance/effects/weapons suffix to outBuf; Creature-kind only; called by Q/E cycle and Shift+H
- L35 — `void HotkeyShiftH()`
  note: Phase 2C — resolves LastTarget, speaks name+brief opener; examine text per-row via TickExaminePanel
- L37 — `void TickExaminePanel()`
  note: logs open/close edges only; speech is owned by HotkeyShiftH (opener) + menus_listbox kExamineSpec (rows)
- L39 — `void PollWin32Hotkey()`
  note: Win32 poll for Action::ExamineOpen (Shift+H); self-gates on player loaded
- L42 — `void SpeakSelfStatus()`
  note: bare-H self-status — HP + active effects (deduped) + equipped weapons; always self, no distance
- L45 — `void PollWin32SelfStatusHotkey()`
  note: Win32 poll for Action::SelfStatusAnnounce (H); gates on player loaded + no blocking UI panel
