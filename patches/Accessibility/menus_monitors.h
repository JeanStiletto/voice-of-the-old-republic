// KOTOR Accessibility — general per-tick monitors.
//
// Post-Step-5 cleanup. Three monitors that watch broad menu state and
// announce changes per tick lift out of menus.cpp:
//
//   * MonitorFocusedControl — re-extracts the focused chain entry's text
//     each tick and speaks the diff against the last snapshot. Catches
//     toggle flips, cycle value changes, slider movement, and any other
//     state visible through extract::FromControl.
//   * MonitorPanelContents — fingerprints content-monitored panels and
//     speaks new segments on change. Handles tutorials, dialogue text,
//     transitions, modal messages, and the in-game sub-screen kind names.
//   * MonitorDialogReplies — polls the dialog reply listbox's selection
//     index on each tick (engine arrow-key navigation mutates it without
//     firing SetActiveControl).
//
// The three subsystem-paired monitors (MonitorContainerSelection,
// MonitorEquipPickerSelection, PollContainerGiveModeKey) live in
// menus_listbox.cpp instead, co-located with the spec entries that own
// the state they watch.
//
// AnnounceControl moves with the focus monitor because it writes the
// monitor's last-seen state (g_focusMonitorControl / g_focusMonitorText)
// to keep speech in sync — they're a unit. menus.cpp's chain-step / Enter
// handlers in OnHandleInputEvent call AnnounceControl through this header.
//
// Sub-screen tracking (InGameSubScreenSpec table, FindInGameSubScreenSpec,
// FindActiveSubScreenPanel, AnnounceNewSubScreens, g_visibleSubScreens)
// also moves here. menus.cpp's drill router needs FindActiveSubScreenPanel
// + a kind predicate, both exposed below.

#pragma once

#include "engine_panels.h"

namespace acc::menus::monitors {

// Per-tick fan-out for the 3 general monitors. Called from TickMonitors
// in menus.cpp, which itself is called from core_tick::Dispatch.
void TickGeneralMonitors();

// Speak a control's announceable text (or "control N" placeholder),
// keeping the focus-monitor's last-seen state in sync. Used by chain-step
// and Enter-on-text-only handlers in OnHandleInputEvent.
void AnnounceControl(void* control);

// Find an InGame{X} sub-screen panel currently in the manager's panels[].
// Returns the lowest-index match or nullptr. Used by the drill router in
// OnHandleInputEvent to retarget the chain from the always-foreground icon
// strip to whichever sub-screen the user just opened.
void* FindActiveSubScreenPanel();

// True if this panel kind is one of the in-game sub-screens reached via
// the icon strip (Inventory, Map, Journal, …). Used by the Esc-drill
// handler to test "is this panel one we drilled into*?" without depending
// on the spec table's struct definition.
bool IsInGameSubScreenKind(acc::engine::PanelKind k);

// Strip-order Tab cycle. Given the currently drilled sub-screen kind and a
// direction (+1 = Tab, -1 = Shift+Tab), return the engine GUI_id of the next
// sub-screen in the icon strip's left-to-right order, with wrap-around.
// Returns -1 when `current` is not a sub-screen kind (defensive: callers
// already gate on the drill flag + FindActiveSubScreenPanel).
int NextStripSubScreenGuiId(acc::engine::PanelKind current, int direction);

}  // namespace acc::menus::monitors
