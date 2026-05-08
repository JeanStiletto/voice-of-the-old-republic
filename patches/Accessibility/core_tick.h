// KOTOR Accessibility — mod-wide per-tick dispatcher.
//
// Owns the OnUpdate hook entry (registered against
// CSWGuiManager::Update @ 0x0040ce76 in hooks.toml). Fans out to each
// subsystem's per-tick callback via Dispatch(), in an order that
// preserves the load-bearing dependencies between them (camera anchors
// yaw → spatial reads camera → view-mode reads spatial cache; pending-
// op drain runs last so action dispatch happens after all observation).
//
// Step 1 of the menus.cpp refactor. Modules are not registered
// dynamically; calls are explicit in Dispatch() so ordering is greppable
// and the call list reads as the canonical source of truth for "what
// fires per tick."

#pragma once

namespace acc::tick {

// Call every per-tick subsystem in canonical order. Invoked from the
// OnUpdate detour but exposed so future tooling (e.g. a forced-tick
// diagnostic) can invoke the same fan-out without reaching into the
// engine hook.
void Dispatch();

}  // namespace acc::tick
