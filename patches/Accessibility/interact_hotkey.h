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

}  // namespace acc::interact
