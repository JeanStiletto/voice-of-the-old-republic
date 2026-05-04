// Phase 2 lay-off 9-probe — in-world cursor-warp / passive-selection monitor.
//
// Layer: probe/ (diagnostic; not part of any pillar's runtime path). Single
// purpose: answer the open RE item documented at
// `docs/navsystems-investigation.md` Q6 §"Open RE — does MoveMouseToPosition
// trigger world-hover?" and inform whether Layer C of the interaction model
// (cursor-warp polish — see `docs/navsystem-longterm-plan.md` "Cross-cutting
// — Interaction model") is on the table for Phase 4 view mode.
//
// Two diagnostic signals, both wired through OnUpdate:
//
// 1. TickMonitor — per-tick poll of CClientExoApp::GetLastTarget @0x005edd80.
//    Logs the handle on change (debounced, last-value cached). Tells us
//    whether `LastTarget` populates organically as the player walks near
//    interactables (i.e. whether passive selection alone is sufficient for
//    Layer A "things come close, hear the name" without any cursor work).
//
// 2. PollHotkey — Alt+P rising edge: logs LastTarget + mouseOverControl
//    before the warp, calls MoveMouseToPosition(mgr, 320, 240) (centre of
//    640×480 reference frame), logs the same fields after. If LastTarget
//    or cursor sprite changes in response to the programmatic warp, the
//    engine's world-hover responds to MoveMouseToPosition and Layer C is
//    a free ride. If they don't, Layer C requires a separate world-hover
//    hook and is likely not worth it.
//
// Probe is read-only beyond the single MoveMouseToPosition call (which is
// already used in production by the menu-nav click-sim path; reentrancy
// risk is the same as the menu callsite — fire from OnUpdate, not from
// inside a manager-level event hook). No game-state mutation. Removable
// in a single commit once the question is resolved.

#pragma once

#include <cstdint>

namespace acc::probe::world_hover {

// Per-tick poll. Reads engine LastTarget; logs on change. Cheap when idle
// (one engine call + one comparison). Self-gates on player-loaded so menus
// / chargen / area-load don't produce spurious deltas.
void TickMonitor();

// OnUpdate per-tick poll for the Alt+P debug hotkey. On rising edge,
// captures before/after engine state around a programmatic
// MoveMouseToPosition(mgr, 320, 240) call. Self-gates on player-loaded +
// foreground-window the same way cycle_input::PollWin32 does.
void PollHotkey();

}  // namespace acc::probe::world_hover

// CClientExoApp::GetLastTarget — __thiscall(void) -> ulong. SARIF
// CONFIRMED. Returns the engine's currently-targeted object handle (the
// game-object-array ID, not a CSWSObject* — resolve via
// engine_area's CGameObjectArray::GetGameObject pattern if pointer
// semantics needed; not required for the probe, which only needs the
// handle value to detect change).
constexpr uintptr_t kAddrCClientExoAppGetLastTarget = 0x005EDD80;
