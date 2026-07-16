// Rakatan temple floor-plate puzzle assist (Unknown World Temple Main
// Floor, module unk_m44ab).
//
// The room holds a 3x3 grid of floor triggers (tags kFloorPanel01..09,
// row-major from the north-west corner) plus a reset trigger
// (kPanelReset) south of the grid. Each plate's OnEnter script
// (k_punk_floor01..09, decompiled 2026-07-16) toggles a lit/dark state
// on ITSELF plus its orthogonal neighbours — a classic lights-out
// puzzle; all nine lit at once opens the massive door (unk44_massdoor).
// The lit state lives as NWScript local boolean index 10 on the visible
// FloorPanel01..09 placeables; the reset script darkens everything.
// There is no required step order and no path tracking engine-side —
// only toggle parity matters.
//
// Sighted players see the plates glow; this module provides the parity
// channel by voice, skill-based (no automation):
//   - one-shot orientation line on approach, plus a solo-mode hint when
//     followers are present (their steps toggle plates too — the
//     scripts never check who entered),
//   - plate entry announcements with the full toggle delta and lit
//     count (plates named by compass position + centre),
//   - continuous nearest-plate announcements while walking the plate-free
//     margins (west lane ~1.4m, east lane pinching to ~0.6m, aprons
//     north/south), with a close-range warning inside ~0.6m,
//   - reset-plate identification with a what-it-does hint,
//   - a solved announcement, after which the module goes quiet (the
//     engine inerts the plates via the UNK_TILES global; we mirror that
//     by observing the all-lit board).
//
// Pure poll (hook-vs-poll principle: state observation), driven from
// core_tick. All engine reads SEH-guarded; inactive outside unk_m44ab.

#pragma once

#include <cstdint>

namespace acc::floor_puzzle {

void Tick();

// True when `handle` is one of the ten puzzle triggers (nine plates +
// reset) in the current area. Used by filter_objects to keep the plates
// out of the Transition cycle category and its proximity sound cue: the
// plate triggers structurally pass IsTransitionTrigger (their
// destination slot holds a plausible-looking strref that resolves to an
// EMPTY TLK entry — same reason cycling spoke their raw tags), and this
// module already provides their entire announcement surface. False
// outside unk_m44ab or before the plate cache is built.
bool IsPuzzlePlateTrigger(uint32_t handle);

}  // namespace acc::floor_puzzle
