# menus_monitors.h (59 lines)

Header for the 3 general per-tick monitors (post-Step-5 cleanup). Documents that the 3 subsystem-paired monitors (Container/EquipPicker/give-mode-key) live in menus_listbox.cpp instead, co-located with the state they watch, and that `AnnounceControl` travels with the focus monitor because they share last-seen-state.

## Declarations (in source order)

- L40 — `void acc::menus::monitors::TickGeneralMonitors()` — called from menus.cpp's TickMonitors, itself called from core_tick::Dispatch
- L45 — `void acc::menus::monitors::AnnounceControl(void* control)` — used by chain-step and Enter-on-text-only handlers in OnHandleInputEvent
- L51 — `void* acc::menus::monitors::FindActiveSubScreenPanel()` — used by the drill router to retarget the chain onto whichever sub-screen just opened
- L57 — `bool acc::menus::monitors::IsInGameSubScreenKind(acc::engine::PanelKind k)` — used by the Esc-drill handler
