// Engine-input pipeline diagnostic hooks — built for the open questions in
// docs/in-game-menu-input-investigation.md (Esc/pause Bug 1 + 2a, both
// resolved). Three upstream-of-manager surfaces share one sequence counter
// so cross-stream correlation is straightforward.
//
// Log streams, one shared counter:
//   * "Diag.ProcInput"  — REMOVED for log volume; the hook still bumps seq
//                         on every frame so frame boundaries are encoded
//                         implicitly as gaps in the streams below.
//   * "Diag.ClientHIE"  — every event the upstream client-app HandleInputEvent sees
//   * "Menus.Input"     — every event the manager HandleInputEvent sees (existing)
//
// Each surviving line carries `seq=N`. To verify the val=1 hypothesis:
// search for a `Menus.Input` line with val=1, then look for a
// `Diag.ClientHIE` line with the same or immediately-preceding seq — if
// present, the manager event was synthesised from upstream; if absent,
// val=1 came from somewhere else.
//
// Layer: diagnostic / engine-side. No menu state, no engine re-entry. Safe
// to leave installed during normal play (sub-100 log lines/sec under heavy
// mashing; the ProcessInput hook is now silent).

#pragma once

namespace acc::diag::input {

// Monotonic counter shared across the three input-stream logs and the
// manager hook (see menus.cpp::OnHandleInputEvent). Bumped on every
// upstream / manager call so the log stream interleaves with comparable seq
// values across surfaces.
unsigned int NextSeq();

}  // namespace acc::diag::input
