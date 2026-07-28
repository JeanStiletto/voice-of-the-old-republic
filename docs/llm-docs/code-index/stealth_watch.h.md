# stealth_watch.h (20 lines)

Declares the stealth-distance readout: while the active leader has Stealth
mode engaged and a hostile creature is the current narrated-target focus,
speaks the changing 2D distance in bare metres so a blind player can judge
Sneak Attack range (under 10m). Inert (silent, read-only, no hooks) in all
other play; driven from the per-tick dispatcher.

## Declarations (in source order)

- L18 — `void Tick()`
