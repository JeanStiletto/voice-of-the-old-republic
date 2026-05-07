// Phase 2 lay-off 9b — combined autowalk+interact hotkey.
//
// Layer: input/ (consumes engine + cycle_state + filter, calls into
// engine click-pipeline). Adds a single user-facing key that takes the
// currently-focused object (cycle focus first, LastTarget fallback) and
// invokes the engine's native click-on-object pipeline against it. The
// engine's own click handler walks the player to the target if needed
// then dispatches the kind-appropriate action: open door / start
// dialog / loot container / pick up item.
//
// Phase 2 lay-off 9b decision (2026-05-04): route through the engine's
// native click pipeline rather than building a CSWSObjectActionNode
// from scratch. Reasons:
//  - The action-node construction path (CSWSObject.action_nodes
//    +0xfc, CSWSObjectActionNode @0x004cac00 + AddActionNodeParameter)
//    is a "PlaceHolder Structure" in Lane's DB — decoding it requires
//    decompiling AddMoveToPointAction's internals to learn the field
//    semantics (kind tag offset, target-handle offset, parameter
//    table layout). That's RE work outside this lay-off's scope.
//  - The engine has a native "click in 3D world" pipeline already —
//    `CClientExoApp::SetLastClickedOnTarget @0x005ee200` plus
//    `CClientExoAppInternal::HandleMouseClickInWorld @0x00620350`. NWScript
//    actions, AI dispatch, walk-to-then-interact are all native
//    behaviour we can trigger by exercising it.
//  - This is the first attempt; diagnostic logging covers all
//    branches so an in-game run tells us exactly what worked vs.
//    what didn't. If HandleMouseClickInWorld doesn't dispatch the
//    expected interaction, we iterate from real data.
//
// Side-channel test of the parked autowalk blocker: if this path
// moves the player when our raw `AddMoveToPointAction` doesn't, the
// engine's native click pipeline is the missing layer and the
// blocker is closeable. Either result is informative.
//
// Hotkey: VK_RETURN (Enter). Self-gates on player-loaded so menus /
// chargen / pre-spawn pass Enter through to the engine's normal
// handlers (dialog confirm, etc.).

#pragma once

#include <cstdint>

namespace acc::interact {

// OnUpdate per-tick poll. Reads VK_RETURN edge state and fires the
// dispatch on rising edge when in-world. Self-gates the same way
// cycle_input::PollWin32 does (foreground window + GetPlayerPosition).
//
// Self-gates additionally on `!acc::view_mode::IsActive()` — when view
// mode is active, view_mode owns Enter routing (the cursor's hover
// channel is the truth for what should be acted on, not cycle_state /
// engine LastTarget). View_mode dispatches into the same flow via
// `DispatchInteract` below, so the Enter behaviour is identical between
// the two modes when a target is hovered.
void PollHotkey();

// Run the Enter / Shift+Enter dispatch flow against an explicit,
// already-resolved target. Public so view_mode can reuse the exact
// same dispatch path PollHotkey uses, just with a different target
// resolution channel (the virtual cursor's hover-pause tracker
// instead of cycle_state / engine LastTarget).
//
// `target` must be the server-side CSWSObject* the user wants to
// interact with. `handle` must be the server-side handle for that
// object (`acc::engine::GetObjectHandle(target)`); the picker writes
// it to the engine's hover/click slots and dispatches.
//
// `forceRadial=true` mirrors Shift+Enter — opens the radial action
// menu instead of dispatching the engine's default action. Engine
// walks the player to the target either way (the radial dispatch
// also walks once the user picks an action).
//
// Behaviour identical to the in-line dispatch PollHotkey ran prior
// to the lay-off-5 refactor: tries the engine action picker first,
// speaks an announce derived from the picker's localised label,
// falls back to AddUseObjectAction on picker failure.
void DispatchInteract(void* target, uint32_t handle, bool forceRadial);

}  // namespace acc::interact
