# intro_skip.h (41 lines)

Header for the intro-movie skip toggle exposed to the in-game mod-settings
panel. State persistence is the filesystem itself (see intro_skip.cpp); the
rename takes effect only on the next launch, so callers should speak a
"takes effect on next launch" cue after flipping.

## Declarations (in source order)

- L27 — `enum class State { Enabled, Disabled, Unknown }`
  note: Unknown means ambiguous/missing folder — caller should treat as Enabled for UI purposes but the toggle call may fail
- L33 — `State CurrentState()`
- L38 — `bool SetDisabled(bool disable)` — renames all three intro files in lockstep; true on success including no-op
