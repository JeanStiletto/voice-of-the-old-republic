// KOTOR Accessibility — public surface of menus.cpp.
//
// Step 1 of the menus.cpp refactor (mod-wide tick dispatcher split).
// menus.cpp is still the monolithic menu-side TU; this header exists to
// let core_tick.cpp call into the menu-side per-tick subsystems without
// reaching into static internals. Subsequent steps split the file
// further.

#pragma once

namespace acc::menus {

// Defensive: drop pointers the engine may have freed since last tick
// (currently the tabbed-panel cluster pointer used by chain navigation).
// Runs first per tick so no later handler dereferences stale state.
void ValidatePanels();

// Per-tick change-detection monitors:
//   * focused control text changed since last tick
//   * panel content snapshot changed
//   * dialog reply listbox selection changed
//   * container loot listbox selection changed
//   * equip-picker listbox selection changed
//   * Win32 G-key poll for container give-mode
void TickMonitors();

// Drain the menu-side pending-op queue (cursor warp, click-sim, fire-
// activate, slider input, equip slot/commit). Runs LAST per tick so no
// monitor sees a partially-applied state.
void TickPendingOps();

}  // namespace acc::menus
