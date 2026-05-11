// Phase 5 lay-off 1 — path-data RE probe.
//
// Diagnostic-only. F9 fires a "synthetic Shift+- without speech": dispatches
// a guidance::WalkTo to a point 10m ahead of the player, then dumps the
// relevant engine state across a tick cascade so we can locate where the
// engine stores its computed path waypoint list. The locked Pillar 3
// design fork hinges on the result: either we read the engine's solution
// directly, or we re-solve A* over the per-area path graph ourselves.
//
// What gets dumped:
//   1. Pre-dispatch, once per press:
//      - CSWSCreature+0x340 region (256 bytes) — the per-creature
//        `path_find_info` block. Investigation Q3 confirms the offset
//        but the layout inside is not decoded. Pre-dispatch should be
//        mostly zeroed (no active path); post-dispatch will show the
//        engine populating the solution somewhere in this window.
//      - CSWSArea+0x23c (path_points) and +0x244 (path_connections).
//        Per investigation, these are the per-area nav graph (CExoArrayList
//        shape: {void* data, int size, int allocated}). We dump the
//        12-byte triple at each offset and a slice of the pointed-to
//        data so we can see node stride + count.
//   2. Post-dispatch, scheduled cascade at t+100ms, t+500ms, t+1500ms,
//      t+3500ms:
//      - CSWSCreature+0x340 region (256 bytes). Diffing against the
//        pre-dispatch dump pinpoints which bytes the pathfinder flipped.
//        CExoArrayList-shaped triples ({non-null ptr, small size, size<=cap})
//        whose pointed data looks like a sequence of Vectors (XYZ float
//        triples with plausible map-coord magnitudes) are the waypoint
//        list candidates.
//
// Hotkey choice — F9:
//   - Unbound by stock kotor.ini (F4=QuickSave, F5=QuickLoad, F8=QuickSave
//     alt; F9 is free).
//   - Same Win32-polling rationale as cycle_input + announce_degrees:
//     unbound keys never reach the manager's HandleInputEvent.
//   - Single key, no modifier — diagnostic, used deliberately, no need
//     to make it hard to hit by accident.
//
// Once the probe has informed the design (single in-game session expected),
// this TU + the F9 hotkey go away. The wired call sites in core_tick are
// the only consumers.

#pragma once

namespace acc::probe_pathfind {

// OnUpdate per-tick poll. Reads VK_F9 via GetAsyncKeyState, edge-detects
// rising edges, self-gates on foreground window + player loaded + area
// resolved. On rising edge:
//   - dumps the pre-dispatch state (CSWSCreature+0x340 region, area's
//     path_points/path_connections triples + sample data),
//   - computes a 10m-ahead target along the player's current heading,
//     dispatches acc::guidance::WalkTo(target),
//   - arms the post-dispatch tick cascade.
void PollWin32();

// OnUpdate per-tick driver for the dump cascade. Cheap when idle (one
// bool check). When armed, fires up to four scheduled dumps at t+100ms,
// t+500ms, t+1500ms, t+3500ms after the dispatch; disarms after the
// final dump or on player-creature loss.
void Tick();

}  // namespace acc::probe_pathfind
