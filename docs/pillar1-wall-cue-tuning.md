# Pillar 1 wall-cue tuning — investigation log

State of the spatial-change-detector wall layer as of 2026-05-07. Bundled checkpoint before context lag — captures what the user reported, what we measured, what we changed, what's still open.

## Problem

Walking through Endar Spire's first corridor produces a permanent wall-cue chatter — 3–4+ wall sounds per step, mostly from "the same direction" (the user's wording). The detector framework is correct (per-feature distance-delta with K-cap), but the cadence is too high to be navigable.

User goals (in order):

- Don't artificially silence — understand the source first
- Cues should fire on *meaningful* distance changes, not on every step
- A long parallel wall on the player's left/right should be silent while walking along it; cues should fire when something *changes* (approach, opening, new feature in range)
- Don't over-filter to the point of losing fine navigation detail
- Don't reduce variety so much that small openings or small course changes go unannounced

## What got measured

Player movement extent over a typical walk-around session:

- Bounding box: ~10 × 7.5 m (single corridor segment / medium room)
- Total path 50–120 m (lots of back-and-forth in that small area)
- Engine tick rate ~30 Hz; max single-tick movement ~0.27 m; player step ≈ 0.6 m

Wall geometry:

- Walkmesh edges per area on Endar Spire: 909–920 (Lane's BWM extraction via `BuildAreaWallCache`)
- Edge length: ~0.7–1.0 m per perimeter segment (corridor wall split into 5–7 segments)

## Mental model

A "wall" the player perceives is multiple short walkmesh perimeter edges laid end-to-end. Tracking each edge independently produces phantom distance changes as the player walks past edge endpoints (closest-point on each edge jumps from "perpendicular foot" to "endpoint" as the player passes). Per-edge tracking can't honour the user's mental model of a continuous wall.

Investigated whether the engine has a higher-level wall-surface API. Findings (full survey in this file's "Engine RE notes" below): the engine has LOS / collision raycast functions (`CSWSArea::ClearLineOfSight`, `CSWCArea::ClearLineOfSightOneWay`, `CSWRoomSurfaceMesh::CheckAABBLineOfSight`) but **no pre-clustered wall-surface enumeration**. Walkmesh edges are the lowest-level data and what we already use.

Two implementation paths considered:

1. **Raycasting in N directions per tick** — engine answers "what's in front of me?" directly. Zero data-structure cost. Risks: 8 unknown parameters on `CSWSArea::ClearLineOfSight`, ray-spacing aliasing.
2. **Cluster edges into surfaces at area-load + track per-surface** — pure code, no engine binding. Each "physical wall" is one entity. Surface count is bounded; per-tick cost identical to per-edge.

Picked clustering — bounded risk, builds on existing `BuildAreaWallCache` infrastructure, gives us extra info (per-surface bearing/extent) for richer cues later. Raycasting kept as a fallback if clustering doesn't behave.

## Implemented

In sequence, with measurements per change:

### 1. Per-surface clustering at area-load (commit pending)

Connected-components on edges, with two predicates per pair:

- **Endpoint coincidence:** edges share an endpoint within 5 cm (XY only — Z noise on seam can otherwise prevent matching)
- **Collinearity:** direction vectors agree within cos(15°) ≈ 0.966 (in absolute value, so reversed traversal still merges)

Union-find, O(N²) at area-load (runs once per zone, ≤100 ms even for 900 edges). Per-tick cost is unchanged: still iterate all edges, but propagate distances into per-surface scratch buffers and threshold-check at the surface level.

Replaces the previous sector-binning + K-cap design (4 cardinal sectors). With surfaces, each surface IS the spatial diversity (each is a distinct physical wall with a distinct bearing); sector binning is no longer needed for "fire one per direction".

### 2. Distance-delta threshold raised 0.5 → 1.5 m

`core_settings.pillar1.distanceDeltaThresholdMeters`. The original 0.5 m was conservatively chosen to never miss a real change, but in practice it's smaller than the player's natural step length (~0.6 m), so every step crossed the threshold for any wall the player was approaching or retreating from.

1.5 m means walls only refire after roughly 2–3 steps' worth of approach/retreat. Trade-off: small fine movements don't get acknowledged.

### 3. Cross-room cluster merge

Dropped the `room_id` equality requirement in the clustering pair test. KOTOR's "room" is an internal `.lyt` segmentation of an area, not a literal room — corridor walls regularly span multiple rooms, and refusing to merge across rooms left long walls fragmented per-room.

Effect was small (~5 % more compression: 916 → 423 vs 920 → 404). Most non-clustered edges are not separated by room — they're separated by *direction* (T/X-junctions where multiple walls meet at a vertex). Each direction is correctly its own surface; the issue was elsewhere.

### 4. Per-tick same-closest-point dedup

T- and X-junction vertices have 3+ edges meeting at one point. Each edge clusters into its own surface (correctly, since each goes off in its own direction). When the player is closest to the *shared corner*, every one of those surfaces independently reports that corner as its closest point, and our K-cap fires several near-identical cues from the same world location.

Implemented at candidate-collection time: any surface whose `best_closest_point` matches a pending candidate within 5 cm merges into that candidate (keeping the smaller distance). The losing surface still has its `last_distance` updated, so it doesn't immediately re-trip the threshold next tick.

## Current measurements

| Metric                          | Before all changes | After all changes |
| ------------------------------- | -----------------: | ----------------: |
| Edge → surface compression      |                  — |   909 → 408 (2.2×) |
| Walls per metre walked          |             ~12.6  |             ~3.07  |
| Same-tick same-position dups    |  Common (~140 ticks) |               0   |
| Tick-distribution: 1/2/3 walls  |  113/95/140        |  89/31/8           |
| Walls fired at top single point | 78 (in 82 m walk)  | 18 (in 58 m walk) |

So the rate per metre dropped from ~12.6 to ~3.1 — 4× improvement overall. The same-position duplicate fires (the worst offender) are entirely gone. But the user still describes it as "pretty much the same" subjectively — meaning even the 3–4× reduced rate is above the threshold of "feels chatty". The chatter is no longer pathological; it's structural.

## What's still firing

Looking at the remaining top-10 positions in the latest log: each fires 4–18 times across a 58 m walk. These are real wall surfaces firing on real distance crossings. The threshold is 1.5 m, so each surface fires roughly every 1.5 m of approach/retreat. Walking 58 m in a 10 × 7 m bounding box means lots of back-and-forth, and any wall the player oscillates relative to fires once per oscillation half-cycle.

So the residual chatter is the system working as designed — distance-delta tracking, fired at the threshold, capped at K=3 per tick. Further reduction has to come from changing the design, not the parameters:

## Options on the table (not yet tried)

1. **Per-surface cooldown.** Same surface can't refire within e.g. 1–2 seconds regardless of motion. Acts as an absolute rate cap — no single wall can spam. Layered on top of the threshold.
2. **Discrete distance shells (zones).** Replace continuous distance-delta with "fire when surface enters a new zone" (e.g. close ≤2 m, mid ≤3.5 m, far ≤5 m). Two fires total per surface as the player approaches, regardless of motion speed; silent while inside one zone.
3. **Speed-gated firing.** Fire only when |Δposition / Δtime| exceeds a minimum — silences the rate when the player is hovering near a wall. (Risk: fast walks produce very few cues.)
4. **Hysteresis on threshold.** Require Δ > 1.5 m one direction before refiring in the same direction; fire instantly on direction reversal. Stops the "oscillation" pattern.
5. **Cap K=2 instead of 3.** Cheap, marginally less chatty.
6. **Increase awareness range from 5 m to 7–8 m.** Wider zone, fewer "newly entering range" first-fires. (Probably small effect.)
7. **Better clustering** (relax cos threshold from 15° to 30° or wider; bigger endpoint tolerance). Expected modest improvement (5–15 %), nothing transformative.
8. **Fall back to raycasting.** If structural reductions still aren't enough, bind `CSWSArea::ClearLineOfSight` and switch to "16 rays from the player, fire on direction-distance change". Removes the entire surface-clustering question. Effort: 30–60 min RE on the 8-parameter signature.

## Engine RE notes — wall / LOS surfaces

Catalogue of relevant engine functions found in Lane's SARIF, kept here so future passes don't re-walk the search:

- `CSWSArea::ClearLineOfSight @ 0x0050c330` — `int(Vector* start, Vector* end, Vector* outHit, ulong* outFlags, ulong p5, ulong p6, int p7, int p8)`. Area-level. 20+ engine xrefs (AI / mouse-pick / ranged-attack codepaths). 8 params, 4 of unknown semantic — likely material masks and "ignore creatures / doors" flags.
- `CSWSRoom::ClearLineOfSight @ 0x005797a0` — `bool(Vector start, Vector end, Vector* outHit, ulong* outFlags)`. Room-only. Cleaner signature; doesn't aggregate across rooms.
- `CSWCArea::ClearLineOfSightOneWay @ 0x00605370` — `int __stdcall(Vector start, Vector end, Vector* outHit)`. 3 params. Simplest. Fewer callsites → less reference for "how it should be called".
- `CSWRoomSurfaceMesh::CheckAABBLineOfSight @ 0x005814d0` — `ulong(Vector, Vector, CSWRoomSurfaceMeshHitInfo*)`. Per-room mesh primitive. `CSWRoomSurfaceMeshHitInfo { material_mask, hitCheckResult, Vector hit_position }`.
- `CSWSArea` also exposes `ClosestPathPoint`, `ComputeAwayVector`, `ComputeSafeLocationInDirection`, `GetSurfaceMaterial` — useful for future work but not for raycasting.

No pre-clustered "wall surface" enumeration. Walkmesh edges are the lowest level of geometry the engine exposes.

## Where to pick up next session

Three reasonable next moves:

1. **Try option 4 (hysteresis on threshold)** — addresses the "oscillation back-and-forth refires" pattern directly without dropping resolution.
2. **Try option 2 (discrete zones)** — biggest qualitative change, matches the user's mental model ("the wall got closer").
3. **Drop in option 8 (raycasting)** — clean-slate alternative if surface-tracking isn't converging.
