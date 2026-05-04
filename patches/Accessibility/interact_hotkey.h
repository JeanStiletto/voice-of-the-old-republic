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

namespace acc::interact {

// OnUpdate per-tick poll. Reads VK_RETURN edge state and fires the
// dispatch on rising edge when in-world. Self-gates the same way
// cycle_input::PollWin32 does (foreground window + GetPlayerPosition).
void PollHotkey();

}  // namespace acc::interact
