// Runtime-toggle helpers for the launch-time intro movies (biologo /
// leclogo / legal). Mirrors installer/IntroMovieDisabler.cs on the
// runtime side so the in-game mod-settings panel can flip the state
// after install.
//
// State persistence is the filesystem itself — no separate config:
//   * If `Movies/biologo.bik` exists → intros play on next launch.
//   * If `Movies/biologo.bik.disabled` exists → intros are skipped.
// We only check biologo.bik as the representative; if intros were
// disabled by the installer all three (biologo / leclogo / legal) were
// renamed together, and the toggle keeps them in lockstep.
//
// The rename takes effect on the NEXT launch — the engine plays movies
// only at startup, so a mid-session toggle won't affect the current run.
// Caller is expected to speak a "takes effect on next launch" cue after
// flipping.

#pragma once

namespace acc::intro_skip {

// Three-state report. Caller uses Disabled vs Enabled to drive the
// toggle UI. Unknown means we couldn't determine the state (Movies
// folder missing, both files present in an inconsistent state, etc.)
// — treat as Enabled for UI purposes so the user can still try to
// toggle, but the underlying call may fail.
enum class State {
    Enabled,    // biologo.bik present, .disabled absent — intros will play
    Disabled,   // .disabled present, biologo.bik absent — intros skipped
    Unknown,    // neither present, or both present (ambiguous)
};

State CurrentState();

// Flip toward the requested target. Renames all three intro files in
// lockstep. Returns true on success (including no-op when already in
// the requested state); false if any rename failed.
bool SetDisabled(bool disable);

}  // namespace acc::intro_skip
