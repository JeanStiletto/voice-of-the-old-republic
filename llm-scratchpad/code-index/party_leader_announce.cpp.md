# party_leader_announce.cpp (99 lines)

Tab leader-change announce implementation. Arms a pending announce on Tab rising-edge (captures pre-press leader pointer), then watches for engine leader-swap over kPendingWindowTicks=6 ticks (~100ms). Announces on pointer change or window expiry.

## Declarations (in source order)

- L10 — `namespace acc::party_leader_announce`
- L39 — `static void Speak()` (anonymous namespace)
  note: reads GetActiveLeaderName and calls prism::Speak(interrupt=true)
- L51 — `void Tick()`
