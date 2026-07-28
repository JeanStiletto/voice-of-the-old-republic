# stealth_watch.cpp (145 lines)

Implements the stealth-distance readout. Reads
`CSWSCreature.stealth_mode` (byte at +0x4d1, Lane-named, confirmed against the
Ghidra RE database) off the player's server creature each tick; on leader
stealth engage it requires the current `narrated_target` slot to be a live,
non-map-pin hostile creature (`examine_view::IsHostileCreature`), computes 2D
floor-plane distance (matches the examine panel's readout), and speaks the
bare integer via `prism::SpeakUrgent` whenever it moves by >= 1m (fine-grained
because stealth movement is slow), gated by a 250ms floor between utterances.
A silent first-tick baseline per newly-acquired focus avoids echoing the name
narration that just spoke. Talks to engine_player, examine_view,
narrated_target, prism.

## Declarations (in source order)

- L23 — `constexpr size_t kCreatureStealthModeOffset = 0x4d1`
- L28 — `constexpr int kStepMeters = 1`
- L31 — `constexpr ULONGLONG kMinSpeakGapMs = 250`
- L33-38 — statics: `g_prevStealth, g_baselineHandle, g_lastAnnounced, g_lastSpeakMs, g_focusHandle, g_lastLoggedDist`
- L40 — `bool ReadLeaderStealth(void* leader)`
  note: SEH-guarded raw byte read
- L50 — `void ResetBaseline()`
- L63 — `int Distance2DMeters(void* target)`
  note: -1 on unresolved position
- L76 — `void Tick()`
  note: full-fidelity per-integer-distance diagnostic log regardless of throttle, per log-no-rate-limits
