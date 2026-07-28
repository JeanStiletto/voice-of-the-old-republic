# menus.h (49 lines)

Public surface of menus.cpp — the entry points `core_tick` calls each frame,
plus the channel-keyed speech dedup shared with the listbox-row hook.

## Declarations (in source order)

- L5 — `namespace acc::menus`
- L9 — `void ValidatePanels()`
  note: called first per tick — drops pointers the engine may have freed (tabbed-panel cluster)
- L13 — `void TickMonitors()`
- L17 — `void PollHomeEndKeys()`
  note: engine drops these keys pre-hook (no [Keymapping] action); Win32-polled and re-dispatched
- L20 — `void TickPendingOps()`
  note: runs LAST per tick so no monitor sees a partially-applied state
- L26 — `void DrainPendingAnnounce()`
- L31 — `void ClearPendingAnnounce()`
- L37 — `void SpeakIfChanged(int channel, const char* text)` / `void MarkSpoken(int channel, const char* text)`
  note: non-static so the focus monitor's voluntary AnnounceControl can prime ch 0's dedup cache
- L46-47 — `bool IsDrilledIntoSubScreen()` / `void SetDrilledIntoSubScreen(bool drilled)`
  note: drill retargets nav from the InGameMenu strip (kept foreground by the engine) to the visible sub-screen
