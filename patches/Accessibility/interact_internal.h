// internal seam between interact_dispatch.cpp and input_poll_router.cpp.
//
// This is NOT public API - interact_dispatch.h stays the surface other
// subsystems (view_mode, the menu hooks) use. The Phase-1 structure pass
// (refactoring candidate 19) moved the per-tick hotkey router out of what
// is now interact_dispatch.cpp, and these are the four helpers it calls.
//
// They were in an anonymous namespace while both halves shared a TU. No
// mutable state crosses the seam - the interact half has none at file
// scope, which is why this split was safe to make.

#pragma once

namespace acc::interact {

// True when a bare interact key should first switch out of the in-game
// menu strip rather than dispatching.
bool ShouldSwitchFromInGameMenu();

// Enter / Shift+Enter interact on the current narrated target.
// forceRadial=true is the Shift+Enter (open the action menu) path.
void OnInteract(bool forceRadial);

// Bare 1-7: announce the personal action-bar slot / target row without
// activating it.
void AnnounceBarePersonalKey(int slot);
void AnnounceBareTargetKey(int row);

}  // namespace acc::interact
