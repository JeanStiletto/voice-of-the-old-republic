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
// Public so view_mode can reuse the exact dispatch path. target must
// be the server-side CSWSObject*, handle the matching server handle
// (engine::GetObjectHandle(target)). forceRadial=true → Shift+Enter
// semantics, open the radial menu instead of dispatching default.
void DispatchInteract(void* target, uint32_t handle, bool forceRadial);

// Interact with the CURRENTLY NARRATED target — the exact gesture the
// keyboard's Enter / Shift+Enter runs, exposed for input sources that do not
// go through the Win32 hotkey poll. The KOTOR 2 gamepad's A button uses it so
// pad and keyboard interact identically (the engine's own pad A fires a
// default action on its LAST-CLICKED target, which for a blind player is not
// the object they were just told about).
//
// The caller owns the context gate: only call this once "in the world, no
// blocking panel, no mod overlay armed" is established, which is exactly what
// the keyboard router checks before calling the same path.
void InteractNarratedTarget(bool forceRadial);
}  // namespace acc::interact
