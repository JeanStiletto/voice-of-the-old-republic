# menus.h (49 lines)

Public surface for the menus accessibility module. Declares namespace `acc::menus`.

## Declarations (in source order)

- L1 — `namespace acc::menus`
- L10 — `void ValidatePanels()`
- L14 — `void TickMonitors()`
- L18 — `void PollHomeEndKeys()`
- L22 — `void TickPendingOps()`
- L27 — `void DrainPendingAnnounce()`
- L37 — `void ClearPendingAnnounce()`
- L41 — `void SpeakIfChanged(int channel, const char* text)`
- L44 — `void MarkSpoken(int channel, const char* text)`
- L47 — `bool IsDrilledIntoSubScreen()`
- L48 — `void SetDrilledIntoSubScreen(bool drilled)`
