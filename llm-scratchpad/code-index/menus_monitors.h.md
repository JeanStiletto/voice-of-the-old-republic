# menus_monitors.h (59 lines)

Public surface for general per-tick monitors (focus, panel-contents, dialog replies, sub-screen tracking). Declares `namespace acc::menus::monitors`.

## Declarations (in source order)

- L1 — `namespace acc::menus::monitors`
- L10 — `void TickGeneralMonitors()` — drains pending announce, runs MonitorFocusedControl / MonitorPanelContents / MonitorDialogReplies, syncs chargen Attribute and Skills selected indices
- L20 — `void AnnounceControl(void* control)` — speak the control's text now; also primes channel-0 dedup and writes monitor state
- L25 — `void* FindActiveSubScreenPanel()` — scans manager panels[] for a known in-game sub-screen kind; returns first match or nullptr
- L28 — `bool IsInGameSubScreenKind(PanelKind k)` — true when k is one of the 8 in-game sub-screen kinds tracked by AnnounceNewSubScreens
