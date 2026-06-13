// Enter / Shift+Enter — interact with the currently focused object.
//
// Routes through the engine's native click pipeline
// (SetLastClickedOnTarget + HandleMouseClickInWorld) rather than building
// a CSWSObjectActionNode ourselves — the action-node format is a
// PlaceHolder Structure in Lane's DB and we don't need to RE it when the
// engine has a working "click on object" entry point that already walks
// + dispatches the kind-appropriate action (open door, talk, loot, pick
// up).
//
// Self-gates on player loaded so menus + chargen pass Enter through.

#pragma once

#include <cstdint>

#include "engine_offsets.h"  // Vector (SpeakWayBlocked target position)

namespace acc::interact {

// Self-gates additionally on !view_mode::IsActive — view mode owns
// Enter routing (cursor hover is the truth, not cycle/LastTarget).
// view_mode dispatches into DispatchInteract below, so behaviour is
// identical when a target is hovered.
void PollHotkey();

// Public so view_mode can reuse the exact dispatch path. target must
// be the server-side CSWSObject*, handle the matching server handle
// (engine::GetObjectHandle(target)). forceRadial=true → Shift+Enter
// semantics, open the radial menu instead of dispatching default.
void DispatchInteract(void* target, uint32_t handle, bool forceRadial);

// Per-tick watchdog for the engine's native walk-then-talk approach. Armed
// when a dialog verb (action 0x3ea) is dispatched with player input left
// enabled (see engine_picker.cpp). Cheap when idle (one flag check). If the
// approach stalls against the walkmesh — the player stops moving while the
// dialog action is still queued and no conversation has opened — it cancels
// the stuck approach, clears dialog-pending state, and announces "way
// blocked". Call once per tick from core_tick.
void TickDialogApproach();

// Announce "way blocked" for an autowalk target the engine couldn't reach,
// with the target's name plus LIVE distance and compass direction to it (so
// the user knows which way to close the gap). Same phrasing the dialog-approach
// guard uses. Falls back to the plain "way blocked" phrase if the player
// position is unreadable. Called by the cycle autowalk guard.
void SpeakWayBlocked(const char* name, const Vector& targetPos);

}  // namespace acc::interact
