# diag_input_pipeline.h (33 lines)

Engine-input pipeline diagnostic hooks built for the open questions in
docs/in-game-menu-input-investigation.md (Esc/pause Bug 1 + 2a, both
resolved). Three upstream-of-manager surfaces share one sequence counter
so cross-stream correlation is straightforward.

Log streams, one shared counter:
- "Diag.ProcInput" — REMOVED for log volume; the hook still bumps seq
  on every frame so frame boundaries are encoded implicitly as gaps
- "Diag.ClientHIE" — every event the upstream client-app HandleInputEvent sees
- "Menus.Input" — every event the manager HandleInputEvent sees (existing)

Each surviving line carries `seq=N`. To verify the val=1 hypothesis:
search for a `Menus.Input` line with val=1, then look for a
`Diag.ClientHIE` line with the same or immediately-preceding seq.

Layer: diagnostic / engine-side. Safe to leave installed during normal play
(sub-100 log lines/sec under heavy mashing; the ProcessInput hook is silent).

## Declarations (in source order)

- L25 — `namespace acc::diag::input`
- L31 — `unsigned int NextSeq()`
  note: monotonic counter shared across all three input-stream logs and the
  manager hook in menus.cpp; bumped on every upstream/manager call; wraps after 4G events
