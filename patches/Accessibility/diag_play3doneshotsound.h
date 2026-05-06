// Diagnostic: log every CExoSound::Play3DOneShotSound fire — resref + caller EIP.
//
// Purpose: identify the per-step humanoid footstep audio caller. Lane's gzf
// has no labelled function for it (Phase 3 lay-off 5 first attempt confirmed
// the *RollingFootstepSound family is vehicle-only — see memory entry
// `project_rolling_footstep_is_vehicle_only.md`). Likely an animation-event
// callback that resolves a `fs_*` resref via surfacemat.2da and calls
// Play3DOneShotSound directly.
//
// Mechanism: detour at 0x005D5E16 (post the entry-time NULL-internal JZ),
// reads CResRef* from [esp+4] and the return address from [esp+0]. Logs one
// line per fire as `Play3DOneShot: caller=0x%08x resref=[%s]`. Offline
// frequency analysis (grep + sort + uniq -c) on patch log identifies which
// EIPs emit `fs_*` strings.
//
// No state to manage; the C-linkage handler `OnPlay3DOneShotSound` is
// installed by the framework via hooks.toml. This header exists only to
// document the diagnostic and pin the file to the build.
//
// Removal: this module is short-lived. Once the caller is RE'd, delete the
// hook + handler + this file in one commit.

#pragma once
