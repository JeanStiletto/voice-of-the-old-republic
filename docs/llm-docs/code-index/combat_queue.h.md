# combat_queue.h (78 lines)

The Shift+H combat action-queue submenu: Open speaks depth, Up/Down cycles entries with clamped focus, Enter removes the focused (tail-only — the engine exposes no positional-remove primitive), Shift+Enter clears all, Esc closes (returning to the unified action menu if it's still open underneath). Also owns the "X, Platz N"/"Warteschlange voll" fire-announce plumbing consumed by the `combat_diag`-hooked `AddAction` detour, gated by a user-press freshness-window latch so engine auto-attacks stay silent.

## Declarations (in source order)

- L20 — `namespace acc::combat::queue`
- L26 — `int CountPlayerEntries()`
  note: raw engine count including the 0xFF placeholder — matches AddAction's own cap check
- L36 — `void ReportPrePressDepth()`
- L41 — `int GetPrePressDepth()`
  note: consume-on-read; -1 sentinel means no bare-key dispatch happened this press
- L50 — `void ArmUserQueueAdd()`
  note: refresh-on-arm, no consume — a rapid press burst all announce
- L61 — `void OnEngineActionAdded(void* combatRound, void* action)`
  note: authoritative announce, called from the AddAction detour
- L65 — `bool Open()`
  note: returns false (speaks "leer") when queue is empty — does not arm
- L67 — `bool IsActive()`
- L70 — `bool HandleInputEvent(int code, int value)`
  note: press-edge only
- L73 — `void ForceDisarm(const char* reason)`
- L75 — `void Tick()`
- L76 — `void PollWin32Hotkey()`
