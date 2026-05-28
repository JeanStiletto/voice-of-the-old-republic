# combat_query.h (47 lines)

Read-only queries over creature stats and the examine panel. No engine re-entry beyond
documented accessors. Each entry self-gates on player loaded.

Surfaces: selected-PC stat block + leader-change auto-announce; cycle/passive-narrate
enrichment for Creature-kind targets; Shift+H Examine; bare-H self-status.

## Declarations (in source order)

- L17 — `namespace acc::combat::query`
- L20 — `bool SpeakSelectedPcStatBlock()`
  note: full HP/FP/AC/attrs/saves/alignment block for the controlled creature; interrupt-speaks
- L23 — `void TickLeaderChangeAutoAnnounce()`
  note: polls active leader name; on change speaks name only (not full stat block — accessor addresses unvalidated until 2026-05)
- L28 — `bool BuildTargetCombatBrief(void* targetServerObject, const char* targetName, char* outBuf, size_t outBufSize)`
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
