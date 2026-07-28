# party_leader_announce.cpp (99 lines)

Speaks the newly-controlled party member's name on Tab rising-edge. Reads on
a *later* tick rather than the press tick because the engine's own input pump
(which swaps `CClientExoApp::GetPlayerCreature` to the next member) runs after
our hotkey edge-detector in `core_tick::Dispatch` — reading immediately would
announce the stale (pre-swap) leader. Talks to `engine_player` (leader/position
accessors), `hotkeys` (Tab edge + foreground gate), and `prism` (speech).

## Declarations (in source order)

- L35 — `constexpr int kPendingWindowTicks = 6`
  note: ~100ms at 60fps; window during which a leader-pointer swap is watched for after Tab.
- L36-37 — `int g_pendingTicks; void* g_pendingLeader`
  note: g_pendingLeader is the pre-press leader pointer, used both to detect the swap and as the solo-party fallback.
- L39 — `void Speak()`
  note: resolves the active leader's name via GetActiveLeaderName and speaks it interrupt=true.
- L51 — `void Tick()`
  note: arms on Tab rising edge (gated on player-loaded via GetPlayerPosition); each subsequent tick checks GetClientLeader() for a pointer change or expires the window (solo-party case) and speaks the current leader either way so every Tab press is audible.
