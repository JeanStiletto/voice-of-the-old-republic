# diag_play3doneshotsound.h (23 lines)

Diagnostic: log every CExoSound::Play3DOneShotSound fire — resref + caller EIP.

Purpose: identify the per-step humanoid footstep audio caller. Lane's gzf
has no labelled function for it (the RollingFootstepSound family is
vehicle-only — see memory entry `project_rolling_footstep_is_vehicle_only.md`).
Likely an animation-event callback that resolves a `fs_*` resref via
surfacemat.2da and calls Play3DOneShotSound directly.

Mechanism: detour at post-entry NULL-internal JZ; reads CResRef* from
[esp+4] and return address from [esp+0]. Offline frequency analysis
(grep + sort + uniq -c) on patch log identifies which EIPs emit `fs_*`
strings.

Removal note (from header): once the caller is RE'd, delete hook + handler
+ this file in one commit. This header exists only to document the
diagnostic and pin the file to the build.

## Declarations (in source order)

(No declarations — header is documentation-only; the C-linkage handler
`OnPlay3DOneShotSound` is defined in the .cpp and installed via hooks.toml.)
