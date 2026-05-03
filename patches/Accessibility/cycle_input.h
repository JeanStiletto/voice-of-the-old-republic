// Pillar 4 cycle input — keypress dispatch into cycle_state.
//
// Layer: input/ (consumes engine_input + cycle_state, mutates the cycle
// singleton, returns a "consumed" verdict to the menu-side hook). Owns the
// shift-modifier tracking for `Shift+,` / `Shift+.` / `Shift+-`.
//
// Phase 2 lay-off 3: wires `,` / `.` / `Shift+,` / `Shift+.` to the cycle
// helpers, and `-` / `Shift+-` to log-only stubs (announce + pathfind land
// in lay-offs 4 / 6 respectively). Speech is intentionally absent here —
// the user-perceptible Pillar 4 milestone is lay-off 4.
//
// Gating: cycle keys only fire when the player is in-game (GetPlayerPosition
// returns true). In menus, chargen, dialog, etc. the keys pass through
// unchanged. This is the cheapest "are we actually playing?" signal we have
// in the engine layer; the alternative (panel-kind sniffing) is more
// invasive and not worth it for a key dispatch decision.
//
// Two ingestion paths:
//
// 1. `TryHandleEvent` — invoked from OnHandleInputEvent. Sees keys the engine
//    routes through CSWGuiManager (i.e. anything bound in kotor.ini's
//    `[Keymapping]`, plus engine logical actions like Up/Down/Confirm/Cancel).
//    Working assumption on raw codes: unmapped keys arrive as their raw
//    InputIndices values (kInputKbComma=103, kInputKbPeriod=104,
//    kInputKbAnnounce=105 [the slash position, labelled `-` on QWERTZ and
//    `/` on QWERTY], kInputKbLeftShift=24, kInputKbRightShift=25).
//
// 2. `PollWin32` — invoked from OnUpdate per-tick. Reads OS-level keyboard
//    state directly via GetAsyncKeyState and edge-detects rising edges.
//    Bypasses the engine's keymap entirely, so unbound keys (the default
//    state for `,/./-` in stock kotor.ini) work in-world. This is the
//    primary path on a stock install; the OnHandleInputEvent path coexists
//    for the case where someone has bound the keys to engine actions, in
//    which case both paths see the press but the per-action handlers
//    debounce via the existing CycleState singleton (no double-effect).
//
// Both paths share the per-action handlers (OnCycleItem / OnCycleCategory /
// OnAnnounceFocus / OnPathfindFocus), so cycle behaviour is identical no
// matter how the keystroke arrived.

#pragma once

namespace acc::cycle_input {

// OnHandleInputEvent path. Returns true if the event was consumed.
// Tracks the engine-side shift flag internally across calls.
bool TryHandleEvent(int param_1, int param_2);

// OnUpdate per-tick poll. Reads VK_OEM_COMMA / VK_OEM_PERIOD / VK_OEM_2
// (the physical key right of `.`) and VK_SHIFT via GetAsyncKeyState, fires
// the per-action handlers on rising edges. Self-gates on
// GetPlayerPosition; in menus / chargen / pre-spawn, no actions fire.
void PollWin32();

}  // namespace acc::cycle_input
