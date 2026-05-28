# party_leader_announce.h (17 lines)

Tab leader-change announce. Speaks the new controlled creature's name on Tab rising-edge. Repetition is intentional — solo Tab confirms "still solo". Fires in panels too (engine strip re-binds to new leader).

## Declarations (in source order)

- L12 — `namespace acc::party_leader_announce`
- L15 — `void Tick()`
  note: foreground + player-loaded gates inside; delayed by kPendingWindowTicks=6 to wait for engine leader-swap
