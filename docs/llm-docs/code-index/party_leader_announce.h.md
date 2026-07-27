# party_leader_announce.h (18 lines)

Header for the Tab leader-change announce. Documents that repetition on a
same-creature (solo-party) Tab press is intentional UX confirmation, and that
the announce is wanted in both world and UI panel contexts since strip panels
re-bind to the new leader too.

## Declarations (in source order)

- L15 — `void Tick()`
  note: foreground + player-loaded gates are internal; caller just calls every tick.
