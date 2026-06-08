#pragma once

// Galaxy / star-map travel screen accessibility (CSWGuiInGameGalaxyMap,
// galaxymap.gui — opened from the Ebon Hawk navigation console).
//
// The panel is a single-axis "cycle one planet, then Travel / Cancel" surface.
// Its planet hotspots are image-only CSWGuiButtons with empty captions (they
// render as "control N" through the generic chain), and the real selection
// state lives server-side on CSWPartyTable, NOT on the panel — so the generic
// chain can neither name the planets nor tell which are hidden.
//
// Instead we drive the engine's own CSWGuiInGameGalaxyMap::HandleInputEvent,
// whose NextPlanet/PrevPlanet handlers already skip planets the party table
// reports as unavailable (not yet revealed) or unselectable (can't travel
// there now). Up/Down cycle the reachable planets and announce each name from
// LBL_PLANETNAME; Enter travels (accept), Esc cancels (back); Shift+Down reads
// LBL_DESC (handled via peek_description). First sight speaks a fixed title
// plus the current planet name.

namespace acc::menus::galaxymap {

// IdentifyPanel(panel) == PanelKind::InGameGalaxyMap.
bool IsGalaxyMapPanel(void* panel);

// Owns Up/Down (cycle planet), Enter (travel), Esc (cancel), and the other
// nav keys (consumed as no-ops) on the galaxy map so the generic chain never
// walks the unnamed planet buttons. Returns true if the panel is the galaxy
// map; sets rv to 1 to consume. Shift+Up/Down are handled earlier by
// peek_description and never reach here.
bool TryHandleInput(void* activePanel, int param_1, int param_2, int& rv);

// Shift+Down peek: speak LBL_DESC for the currently displayed planet. Returns
// true if the panel is the galaxy map (caller consumes the key regardless).
bool SpeakDescription(void* panel);

// Deferred engine dispatch, invoked from the pending-op Drain. Calls the
// panel's HandleInputEvent(engineCode, 1); when announcePlanet is set (Up/Down
// cycles), re-reads LBL_PLANETNAME afterwards and speaks the new planet name.
void DispatchInput(void* panel, int engineCode, bool announcePlanet);

// First-sight announce: when the galaxy map newly appears in panels[], speak
// the title + current planet name. Called once per tick from TickMonitors.
void Tick();

}  // namespace acc::menus::galaxymap
