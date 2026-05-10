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

// Drill-flag accessors (state lives in menus.cpp).
//
// `g_drilledIntoSubScreen` controls whether the chain router retargets
// arrow-key navigation from the InGameMenu strip (where focus naturally
// lands due to engine's strip-stays-fg pattern) to the actual visible
// sub-screen content. Originally armed only when the user pressed Enter
// on a strip icon. Sub-screens that open via direct paths (Esc → pause,
// M → map, I → inventory etc.) bypass that arm, leaving chain nav
// pointing at the strip instead of the just-opened sub-screen — the
// user perceives this as "menu won't navigate" because the sub-screen
// has no chain anchor.
//
// The SubScreen monitor uses these to auto-arm drill on any newly-
// detected sub-screen, so direct-open paths get the same chain
// retargeting behaviour as strip-icon Enter.
bool IsDrilledIntoSubScreen();
void SetDrilledIntoSubScreen(bool drilled);

}  // namespace acc::menus
