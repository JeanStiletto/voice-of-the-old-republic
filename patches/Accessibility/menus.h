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

// Win32 poll for Home / End rising edges. The engine's keymap drops
// KEYBOARD_HOME / KEYBOARD_END before our manager hook because no
// kotor.ini [Keymapping] action targets them — same pattern as the cycle
// keys. On a rising edge, this synthesises a call to OnHandleInputEvent
// with kInputHome / kInputEnd as param_1, which re-enters the listbox
// dispatcher + editbox dispatcher + chain navigation pipeline exactly
// as a real keypress would. Called from core_tick::Dispatch alongside
// the other Win32 pollers.
void PollHomeEndKeys();

// Drain the menu-side pending-op queue (cursor warp, click-sim, fire-
// activate, slider input, equip slot/commit). Runs LAST per tick so no
// monitor sees a partially-applied state.
void TickPendingOps();

// Pending-announce slot drain. Hook handler `OnSetActiveControl` writes
// the slot on every panel-level focus event; this drains it once per tick.
// Runs early in TickMonitors so multiple intra-tick SetActive events
// collapse to a single announcement (last write wins). The hook never
// speaks directly — that was the source of the "OK, Abbrechen" double-
// announce on MessageBox open, where the engine fires NULL → OK →
// Abbrechen back-to-back during init and the hook spoke each one.
void DrainPendingAnnounce();

// Drop the pending slot without speaking. For subsystems that took over
// announcing a focus event with their own format (e.g. editbox arm
// speaking "Editbox. <value>") and want to suppress the drain's plain-text
// re-announce of the same control.
void ClearPendingAnnounce();

// Channel-keyed speak-if-text-changed dedup, shared between the panel-
// focus drain (channel 0) and the listbox-row hook (channel 1).
// Non-static so the focus-monitor's voluntary AnnounceControl can prime
// channel 0's last-spoken cache via MarkSpoken — that's what stops the
// engine's post-nav SetActive echo from re-announcing the same control
// the chain handler just spoke.
void SpeakIfChanged(int channel, const char* text);
void MarkSpoken(int channel, const char* text);

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
