# Pillar 1 wall-cue tuning — investigation + tuning log

State of the spatial-change-detector wall layer. **Latest tuning session 2026-05-07 landed the first noise level the user reported as comfortable**; system is good enough to keep playing. Future tuning will revisit. Doc preserves the alternatives we considered but didn't pick — raycasting, zones, speed-gating, cluster-threshold widening — so a later session can pick from them without redoing the analysis.

---

## Current state (2026-05-07)

Latest tuning session in commit time order. All shipped to `patches/Accessibility/spatial_change_detector.cpp`; the user verified each step in-game and signed off ("way quieter now ... I would try this now").

### What's shipped

**T1 wall pipeline** (per cardinal sector, world-frame):

- Per-sector continuous distance-delta, no zones. Each of the 4 sectors (E/N/W/S) tracks one surface (its closest in that quadrant) and refires when `|current − last_fired| > distanceDeltaThresholdMeters` against the *same* tracked surface.
- **Silent retracks on enter / exit / identity-swap.** A sector entering range, leaving range, or switching which surface is closest does *not* fire — those are bookkeeping events, not real-world events. The next genuine threshold crossing on the new tracked surface fires naturally as the player keeps moving; 3D audio puts it at the right distance/bearing without an explicit entry ping.
- **Range hysteresis** (`kAwarenessRangeHysteresisMeters = 0.3 m`). Untracked surfaces enter at 5.0 m; tracked surfaces leave at 5.3 m. Stops boundary flap when a wall sits right on the line.
- **Per-sector cooldown** (`kSectorCooldownMs = 1000 ms`) with `last_fired_distance` pinned on suppressed samples — so a retreat during cooldown doesn't fire perceptually backwards.
- **K-cap = 3** sectors per tick, sorted by ascending distance (closer wins if ≥3 sectors transition same tick).
- **Per-tick same-closest-point dedup** still active — surfaces sharing a T-/X-junction vertex collapse into one cue.

**T2 (foremost-in-front) wall fires** are now strongly gated:

- **(C) Only `none → wall` and `obj → wall` transitions fire.** `wall → wall` (player panned and a different wall is now foremost) is silently retracked. Rationale: T1 already announces per-direction wall changes; T2's value is the cone-crossing event itself, not which specific surface is currently foremost.
- **(B) Suppressed within T1's wall cooldown window** — if any T1 sector fired a wall in the last 1000 ms, T2 wall is suppressed even if the surfaces differ. The player is already getting wall info this beat.
- The original 250 ms same-surface guard (`kT2QuietMs`) remains layered on top.
- T2 object fires are unchanged — objects carry distinct semantic content not duplicated by T1.
- Diagnostic line `ChangeDetector: T2 wall blocked coneEnteredWall=… t1Quiet=… surfaceQuiet=… …` fires when the foremost wall settled but a gate suppressed it. Lets us tell "no T2 wall fires because the cone never sees one" (correct silence) from "T2 wanted to fire but was gated" (also correct, just visible in logs).

**Object pipeline** (Door/NPC/Container/Item/Landmark/Transition) mirrors the wall pattern after the 2026-05-07 alignment:

- Silent retrack on first observation in range — no entry ping at the bubble edge.
- Range hysteresis (same `rangeExit`).
- Per-object 1 s cooldown using `kSectorCooldownMs`, with `last_distance` pinned on suppressed samples.
- No K-cap on objects (object population is sparse enough that all crossings should fire).

### Measurements at lock-in

| Run | Total cues / sec | Wall cues | Object cues | Notes |
|-|-|-|-|-|
| Pre-2026-05-07 baseline | 4.3 | 49 (40%) | 72 (60%) | Object-pipeline entry pings + no cooldown |
| After object align | 2.0 | 47 (63%) | 28 (37%) | Walls dominate; T2 walls leaked through dedup |
| After T2 (B+C) gates | ~2.0 | 30 (44% — 47% wall surfaces in range) | 38 (56%) | User: "way quieter now" |

Reporting note: walking density varied between runs, so absolute totals aren't strictly comparable; what matters is the per-cue-class ratio and the per-second rate. Final state: ~1.8–2.0 cues/sec average in dense Endar Spire corridors with 9–28 wall surfaces in range.

### Parameters live in code

- `core_settings.pillar1.awarenessRangeMeters` — 5.0 m
- `core_settings.pillar1.distanceDeltaThresholdMeters` — 1.5 m
- `core_settings.pillar1.trigger1MaxWallCuesPerTick` — 3
- `kSurfaceCollinearityCosThreshold` — cos(15°) ≈ 0.966
- `kEndpointTolMeters` — 0.05 m (5 cm)
- `kFireDedupTolMeters` — 0.05 m (5 cm)
- `kSectorCooldownMs` — 1000 ms (shared by T1 sectors and objects)
- `kAwarenessRangeHysteresisMeters` — 0.3 m
- `kT2QuietMs` — 250 ms (foremost stability window + same-surface guard)

---

## Problem statement (history)

Walking through Endar Spire's first corridor produced a permanent wall-cue chatter — 3–4+ wall sounds per step, mostly from "the same direction." The detector framework was correct (per-feature distance-delta with K-cap), but the cadence was too high to be navigable.

Original user goals (preserved as the design constraint):

- Don't artificially silence — understand the source first
- Cues should fire on *meaningful* distance changes, not on every step
- A long parallel wall on the player's left/right should be silent while walking along it; cues should fire when something *changes* (approach, opening, new feature in range)
- Don't over-filter to the point of losing fine navigation detail
- Don't reduce variety so much that small openings or small course changes go unannounced

---

## Mental model

A "wall" the player perceives is multiple short walkmesh perimeter edges laid end-to-end. Tracking each edge independently produces phantom distance changes as the player walks past edge endpoints (closest-point on each edge jumps from "perpendicular foot" to "endpoint" as the player passes). Per-edge tracking can't honour the user's mental model of a continuous wall.

Investigated whether the engine has a higher-level wall-surface API. Findings (see §Engine RE notes below for the catalog): the engine has LOS / collision raycast functions but **no pre-clustered wall-surface enumeration**. Walkmesh edges are the lowest level of geometry the engine exposes.

Two implementation paths considered:

1. **Raycasting in N directions per tick** — engine answers "what's in front of me?" directly. Zero data-structure cost. Risks: 8 unknown parameters on `CSWSArea::ClearLineOfSight`, ray-spacing aliasing.
2. **Cluster edges into surfaces at area-load + track per-surface** — pure code, no engine binding. Each "physical wall" is one entity. Surface count is bounded; per-tick cost identical to per-edge.

**Picked: clustering** — bounded risk, builds on existing `BuildAreaWallCache` infrastructure, gives extra info (per-surface bearing/extent) for richer cues later. **Raycasting parked** as a fallback if clustering didn't behave.

---

## Implementation history

In sequence, with measurements per change. The earlier shipped behaviour evolved several times before settling at the "Current state" section above; this section preserves the trajectory so a future session understands why the design ended up where it did.

### 1. Per-surface clustering at area-load

Connected-components on edges, with two predicates per pair:

- **Endpoint coincidence:** edges share an endpoint within 5 cm (XY only — Z noise on seam can otherwise prevent matching)
- **Collinearity:** direction vectors agree within cos(15°) ≈ 0.966 (in absolute value, so reversed traversal still merges)

Union-find, O(N²) at area-load (runs once per zone, ≤100 ms even for 900 edges). Per-tick cost is unchanged: still iterate all edges, but propagate distances into per-surface scratch buffers and threshold-check at the surface level.

### 2. Distance-delta threshold raised 0.5 → 1.5 m

`core_settings.pillar1.distanceDeltaThresholdMeters`. The original 0.5 m was conservatively chosen to never miss a real change, but in practice it's smaller than the player's natural step length (~0.6 m), so every step crossed the threshold for any wall the player was approaching or retreating from.

1.5 m means walls only refire after roughly 2–3 steps' worth of approach/retreat. Trade-off: small fine movements don't get acknowledged.

### 3. Cross-room cluster merge

Dropped the `room_id` equality requirement in the clustering pair test. KOTOR's "room" is an internal `.lyt` segmentation of an area, not a literal room — corridor walls regularly span multiple rooms.

Effect was small (~5% more compression: 916 → 423 vs 920 → 404). Most non-clustered edges are not separated by room — they're separated by *direction* (T/X-junctions where multiple walls meet at a vertex). Each direction is correctly its own surface.

### 4. Per-tick same-closest-point dedup

T- and X-junction vertices have 3+ edges meeting at one point. Each edge clusters into its own surface (correctly). When the player is closest to the *shared corner*, every one of those surfaces independently reports that corner as its closest point, and the K-cap fired several near-identical cues from the same world location.

Implemented at candidate-collection time: any surface whose `best_closest_point` matches a pending candidate within 5 cm merges into that candidate (keeping the smaller distance). The losing surface still has its `last_distance` updated, so it doesn't immediately re-trip the threshold next tick.

### 5. Per-surface zones (built then replaced)

Tried discrete zones — Open / Far / Mid / Close — per clustered surface, with per-zone-transition fires and per-surface cooldown. Borders 1.5 / 3 / 5 m chosen against the empirical KOTOR scale (see §Empirical spatial scale below).

Replaced before final lock-in by the simpler **per-sector continuous distance-delta** model (current state). Reason: zones added complexity without adding meaning — they were pure firing gates, expressible as a single threshold against the last fire. The zone borders + hysteresis + cooldown collectively did the same job as `|current - last_fired| > threshold` + cooldown. Sector binning replaced surface binning as the cue-grouping mechanism.

### 6. World-frame sectors (replaced player-relative)

Initial sector binning was relative to player heading. KOTOR's character body slowly rotates to face movement direction even with no rotate-key input, so player-relative sectors treated walking as world-rotation, producing cascade enter/exit fires every few steps.

World-frame sectors only shift when the player's *position* moves enough to put a wall on the other side of a 90° world quadrant — much rarer than yaw drift. T2's "is this in front?" cone is genuinely player-relative and uses the same classifier with a different bearing input.

### 7. Silent retracks on enter / exit / identity-swap (2026-05-07)

Removed the explicit "wall came into range" / "wall left range" pings. Rationale: an entry ping at 4.99 m on the bubble edge mostly announces walls the player will walk past at the bubble's edge anyway; the next threshold crossing on actual approach fires naturally with correct 3D audio distance. Exit silence is itself the signal.

11/45 (~24%) of T1 fires in the pre-change log were enter/exit pings. After: 0.

### 8. Object pipeline aligned (2026-05-07)

Objects (Door/NPC/Container/Item/Landmark/Transition) had been firing on `isNew` (entry into range) without cooldown or hysteresis. Mirrored the wall fixes: silent retrack on first observation, range hysteresis, 1 s per-object cooldown with pinned `last_distance` on suppressed samples.

Effect: object cues 72 → 28 in comparable runs (–61%).

### 9. T2 wall gates (2026-05-07)

The 250 ms same-surface dedup leaked badly — 12/15 T2 wall fires happened in the same second as a T1 wall fire. Two new gates layered on top:

- **(B) Global T1 wall cooldown.** New `g_t1_wall_last_fired_at` stamped on every successful T1 wall fire. T2 walls require ≥1000 ms since the last T1 wall.
- **(C) Cone transition shape.** T2 walls only fire on `none → wall` and `obj → wall`. `wall → wall` is silently retracked.
- **Diagnostic.** `ChangeDetector: T2 wall blocked …` log line emitted when foremost settles to a wall but a gate suppresses it. Visible distinction between "cone never sees a wall" and "T2 wanted to fire but was gated."

Both gates evaluated correct at user verification: walking around dense corridors produced 4 T2 fires across 38 sec (was 14–16 before), all of them on meaningful cone transitions (entered a wall after seeing an object, etc.). User reading: "no T2 fires because the cone is always-wall in this corridor" — confirmed correct via the diagnostic.

---

## Empirical spatial scale of KOTOR (2026-05-07)

To inform thresholds / zone borders / raycast spacing without guessing, ran `kdev walkmesh-stats` over every walkmesh in the game (102 areas, 1202 .wok files, 39,346 perimeter edges, 19,350 corridor-width samples). Walkable face filter via `surfacemat.2da`; "corridor width" = perpendicular distance from each perimeter edge to the nearest parallel-and-facing opposing edge in the same room. See `tools/kdev/Commands/WalkmeshStatsCommand.cs`.

With **0.5 m artifact floor** (lenient — probably some sliver bleed-through):

- Global min: 0.51 m
- p1: 0.90 m
- p10: 2.89 m
- p50 (median): 7.48 m
- Tightest area: m09aa (Taris Lower City) at 0.51 m

With **1.0 m artifact floor** (strict — definitely navigable):

- Global min: 1.00 m (filter floor)
- p1: 1.12 m
- p10: 3.05 m
- p50: 7.50 m

Endar Spire (m01ab) specifically:

- p10: 2.02 m, p50: 3.40 m, p90: 15.30 m
- Smallest passage: 1.40 m (a doorway in m01ab_10a)

**Conclusions:**

- 0.5 m hidden gaps don't exist. Anything under ~0.9 m is almost certainly a walkmesh artifact rather than a passage. Player PERSPACE alone (~0.13 m radius → 0.26 m diameter collision) sets a hard floor below this anyway.
- Smallest realistic passage ≈ 1.0 m, very rare — typical doorway is 1.5–2 m, typical corridor 2.5–3.5 m, typical room 7.5 m.
- 1.5 m distance-delta threshold and 5 m awareness range are well-aligned to KOTOR's spatial scale.
- 16 rays (22.5° spacing) marginal at 5 m for 1 m gaps. A 1 m doorway at 5 m subtends 11.5° — falls between two rays if perfectly centred. At 2.5 m it subtends 22.9° (covered by one ray); at 1.5 m, 38° (covered by two). 32 rays (11.25°) reliably cover every real passage; 16 may miss the rare 1 m doorway only at maximum awareness range.
- Cluster fragmentation in m01ab is real but not catastrophic. 909 edges → 408 surfaces is already 2.2× compression; with edge length p50 = 0.95 m on Endar Spire, surface clustering catches most real walls.

---

## Parked options for future tuning

These were considered and not picked (or picked then replaced) but remain valid moves if the user wants to revisit. Listed roughly in order of expected effort vs effect.

1. **Per-surface absolute-rate cap.** Same surface can't refire within 1–2 s regardless of motion. Acts as an absolute rate cap layered on top of the threshold. Currently we have a per-sector cooldown but not a per-surface one; if a single physical wall straddles two sectors as the player moves, it could conceivably fire from each in quick succession.
2. **Discrete distance shells (zones).** Tried and replaced (§5 above) but valid as a future variant if "fire on zone change" feels more useful than "fire on threshold delta." Border candidates already calibrated: Close ≤1.5 m, Mid 1.5–3 m, Far 3–5 m.
3. **Speed-gated firing.** Fire only when `|Δposition / Δtime|` exceeds a minimum — silences the rate when the player hovers near a wall. Risk: fast walks produce very few cues; tune live.
4. **Direction-reversal hysteresis.** Require Δ > 1.5 m one direction before refiring in the same direction; fire instantly on reversal. Stops the "oscillation refire" pattern even more aggressively than the current cooldown.
5. **Cap K = 2 instead of 3.** Cheap, marginally less chatty. Would only help when the player enters a cluster where 3+ sectors transition the same tick.
6. **Increase awareness range from 5 m to 7–8 m.** Wider zone, fewer "newly entering range" first-fires. Probably small effect now that entry is silent.
7. **Wider clustering thresholds** (cos threshold from 15° to 30°, larger endpoint tolerance). Expected modest improvement (5–15%); fewer surfaces, more aggressive merging across slight bends.
8. **Distance-gate T2 walls** (e.g. only fire if foremost wall < 2.5 m). Soft variant of gate (C): retain the "wall right in front of you" signal, drop mid-distance pan chatter even on `none → wall` transitions.
9. **Drop T2 walls entirely.** T2 still works for objects (NPCs/doors in front) where it's more useful — "what am I looking at?" Cleanest cut if the (B+C) gates still feel chatty after extended play.
10. **Fall back to raycasting.** If structural reductions still aren't enough, bind `CSWSArea::ClearLineOfSight` (or its variants below) and switch to "N rays from the player, fire on direction-distance change." Removes the entire surface-clustering question. Effort: 30–60 min RE on the 8-parameter signature.

---

## Engine RE notes — wall / LOS surfaces

Catalogue of relevant engine functions found in Lane's SARIF, kept here so future raycasting / clustering passes don't re-walk the search:

- `CSWSArea::ClearLineOfSight @ 0x0050c330` — `int(Vector* start, Vector* end, Vector* outHit, ulong* outFlags, ulong p5, ulong p6, int p7, int p8)`. Area-level. 20+ engine xrefs (AI / mouse-pick / ranged-attack codepaths). 8 params, 4 of unknown semantic — likely material masks and "ignore creatures / doors" flags.
- `CSWSRoom::ClearLineOfSight @ 0x005797a0` — `bool(Vector start, Vector end, Vector* outHit, ulong* outFlags)`. Room-only. Cleaner signature; doesn't aggregate across rooms.
- `CSWCArea::ClearLineOfSightOneWay @ 0x00605370` — `int __stdcall(Vector start, Vector end, Vector* outHit)`. 3 params. Simplest. Fewer callsites → less reference for "how it should be called."
- `CSWRoomSurfaceMesh::CheckAABBLineOfSight @ 0x005814d0` — `ulong(Vector, Vector, CSWRoomSurfaceMeshHitInfo*)`. Per-room mesh primitive. `CSWRoomSurfaceMeshHitInfo { material_mask, hitCheckResult, Vector hit_position }`.
- `CSWSArea` also exposes `ClosestPathPoint`, `ComputeAwayVector`, `ComputeSafeLocationInDirection`, `GetSurfaceMaterial` — useful for future work but not for raycasting.

No pre-clustered "wall surface" enumeration. Walkmesh edges are the lowest level of geometry the engine exposes.

---

## Where to pick up if revisited

Default move if the user feels the system is still chatty after extended play: option (8) distance-gate T2 walls, then (1) per-surface absolute-rate cap, then (3) speed-gating. Each is layered on top of the current design without invalidating it.

Default move if the user feels the system is too quiet (missing wall info that should fire): lower the threshold from 1.5 m to 1.0 m as a first step — that's a single constant in `core_settings.h` and reverses the most aggressive cut from the original tuning.

Raycasting (option 10) is a clean-slate alternative if surface-tracking turns out to fundamentally not behave; it removes the clustering complexity at the cost of an RE pass on `ClearLineOfSight`. Defer until evidence the current path can't be tuned further.
