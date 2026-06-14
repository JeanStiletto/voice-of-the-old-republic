---
name: Walltopo successor — three candidate paths, Path 3 preferred
description: Shape-classification successor for region_classifier after iterations on wall-pairs + face flood-fill failed. Three candidate paths mapped to academic taxonomy; Path 3 (BioWare nav-graph topology) chosen as the next implementation.
type: project
originSessionId: 0622f49d-f2aa-4328-a903-1c76e27afcd6
---
After three iterations on shape-classification (wall-pair detection, face flood-fill — both empirically failed) we mapped the problem onto the room-segmentation literature (Bormann et al. Fraunhofer 2016, Mozos et al. Freiburg AdaBoost 2004, van Toll et al. Explicit Corridor Map). Three candidate paths emerged; Path 3 is the preferred next implementation.

## Why: prior iterations both produced 100% "Offene Fläche" fallback
- Walltopo wall-pair (Phase 4 of original walltopo): K1 corridors too wide for "player walks between two close walls" assumption; raising the 6m cap breaks rooms-as-corridors. Wall identity alone has no shape signal.
- Walkable-face flood-fill (today's iteration): K1 walkable space is too connected through doorways — Apartments 17 rooms → 1 cell, Oberstadt 22 rooms → 2 cells. Connectivity alone has no shape signal.
- Both failures converge: shape signal is *local geometry around a sample point*, not wall geometry or connectivity in isolation.

## Path 1 — raycast Flavor A (per-face 8-ray probe at area-load)
Walk every walkmesh face from `BuildAreaFaceCache`. From each face's centroid, cast 8 rays (N/NE/E/SE/S/SW/W/NW) against the cached wall edges. Read distance pattern → derive label (Korridor + axis + width, Kreuzung + directions, Sackgasse + direction, Offene Fläche). One label cached per face. Lookup = point-in-face via spatial grid (already built today).

Academic equivalent: feature-based room segmentation (Mozos/Burgard AdaBoost, but hand-coded thresholds instead of ML). Published accuracy 92.1%.

Pros: known-good signal (matches region_classifier's working probe), zero per-tick cost, simple. Cons: per-face granularity can produce label "noise" at face boundaries; ML smoothing not available.

## Path 2 — Generalized Voronoi Diagram / medial axis
Compute the skeleton of the walkable region — locus of points equidistant from 2+ walls. Points equidistant from 2 walls = corridor centerline; branch points (3+ equidistant walls) = junctions; skeleton endpoints = dead ends.

Academic equivalent: GVD-based segmentation (van Toll et al.'s Explicit Corridor Map; commercial pathfinding uses this). Captures the "walls cornering / walls ending to open a passage" intuition mathematically. Cleanest mathematical approach; produces topology graph for free.

Pros: mathematically rigorous, no thresholds, topology graph reusable for pathfinding. Cons: medial axis computation non-trivial code; walkmesh dual graph helps but doesn't replace the algorithm.

## Path 3 — BioWare nav-graph topology (PREFERRED)
Use `CSWSArea.path_points` + `CSWSArea.path_connections` (see memory:project_kotor_nav_graph_layout) as the topology source. Classify each nav point by its CSR degree:
- Degree 1 → dead-end (with bearing toward single neighbour)
- Degree 2 → corridor interior (with axis from one neighbour to the other, length = chain length)
- Degree ≥ 3 → junction (with bearings to all neighbours)

At runtime: nearest nav point to player position → return its cached label.

Academic equivalent: the nav graph is what a level designer hand-placed approximating a medial-axis topology. Game industry already uses this kind of graph for AI navigation; we're just deriving labels from the same structure.

Why preferred:
- The data is already there — same nav graph we use for Pillar 3 beacon pathfinding.
- 51-104 nodes per area (vs 600-3000 faces) — small enough for trivial linear-scan lookup.
- Designer-validated positions — no centroid-in-wall, no sliver problems.
- BioWare placed nodes to capture the level's intended topology; we get that for free.
- No threshold tuning, no probe geometry, no spatial index needed.
- Lowest implementation effort of the three.

Cons: spatial granularity ~3-5m (nav-node spacing) — fine for announcements, possibly coarse for view-mode cursor. Some nav placements may be AI-driven rather than perception-driven — needs in-game validation per area type.

## Architectural ordering for future iterations
- Path 3 first. If it works, ship and move on.
- Path 1 as fallback if nav-graph density is wrong-shaped in some areas (open zones with sparse coverage).
- Path 2 only if Path 1 and Path 3 both prove inadequate, or if a future feature needs a topology graph independent of BioWare's authoring.

## What gets kept regardless of which path we pick
- Pillar 1 wall surface clustering in `spatial_change_detector.cpp` — serves Pillar 1 wall sounds, unchanged.
- Walkmesh-quirk filter in `engine_area.cpp` (step/slope dedup, non-manifold, multi-elevation) — useful to anything touching walkmesh.
- `region_classifier` (8-ray probes per .lyt-room centroid) — kept as silent comparison logger for new system validation.
- `guidance_pathfind` nav graph reader (Pillar 3 beacon) — Path 3 reuses this data path directly.

## What gets removed when Path 3 lands
- Walltopo wall-pair detection (Phase 4 of original walltopo) — already deleted in today's rewrite.
- Today's cell flood-fill code in `wall_topology.cpp` — replaced by nav-graph degree classification.
- `BuildAreaFaceCache` + `WalkmeshFace` struct + face spatial index — added today, unused by Path 3. Delete unless a future caller needs it (recoverable from git history if so).

## Testing pattern when Path 3 lands
- Path 3 = production speech (drives `SpeakRoomChange`).
- `region_classifier` = silent comparison logger via `LogWallTopoComparison`. Logs Path 3 verdict + room-classifier verdict side-by-side every announce, so we can see where they agree/disagree without inflicting Path 3 bugs on the user.
- Flip production back to room-classifier if Path 3 produces wrong labels in actual play.
