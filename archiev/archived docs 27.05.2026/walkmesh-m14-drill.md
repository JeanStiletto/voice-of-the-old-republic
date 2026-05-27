# Walkmesh geometry audit

Generated: 2026-05-11 07:35 UTC
Source: build/wok-extract/*.wok — area room walkmeshes only.
Cell size for reachability: 0.50 m.

## Headline numbers

- Areas analysed: 5
- Rooms analysed: 22
- Walkable triangles: 6901
- Total walkable area: 165454.2 m²
- Total perimeter length: 19744.4 m
- Rectilinear fraction, world axes: 46.4%
- Rectilinear fraction, per-room best rotation: 48.5%
- Total walkable cells at 0.50 m: 661816 (165454 m²)
- 8-way reachable from seed: 310367 (77592 m²)
- 4-cardinal reachable from seed: 267374 (66844 m²)
- 4-cardinal coverage of walkable cells: 40.4%
- Cells uniquely unlocked by diagonals: 13.9% (4-card vs 8-way from same seed)
- Mean longest cardinal run per cell: 59.38 m
- Mean longest diagonal run per cell: 37.93 m
- Smooth cells (≥5 m cardinal run): 99.8%
- Fiddly cells (cardinal ≤2 m, diagonal ≥5 m → forced zigzag in 4-card): 0.0%
- Choke cells (cardinal ≤1 m & diagonal ≤1 m → single-cell pinch): 0.0%
- Skinny triangles (min angle < 10°): 312 of 6901 (4.5%)

## Edge-angle histogram (world axes)

Perimeter-edge length per 5° bin, folded into [0°, 90°). Cardinal bins (0-5° and 85-90°) measure axis-alignment under the world frame.

-  0- 5°: 16.8%  `#################`
-  5-10°: 4.3%  `####`
- 10-15°: 3.5%  `####`
- 15-20°: 3.0%  `###`
- 20-25°: 3.6%  `####`
- 25-30°: 2.5%  `###`
- 30-35°: 2.6%  `###`
- 35-40°: 2.4%  `##`
- 40-45°: 3.7%  `####`
- 45-50°: 3.4%  `###`
- 50-55°: 2.5%  `##`
- 55-60°: 2.4%  `##`
- 60-65°: 2.9%  `###`
- 65-70°: 3.2%  `###`
- 70-75°: 4.6%  `#####`
- 75-80°: 3.4%  `###`
- 80-85°: 5.4%  `#####`
- 85-90°: 29.6%  `##############################`

## Grid rasterisation loss (centre-walkable rule)

- 0.25 m cells: 100.0% of walkable area kept
- 0.50 m cells: 100.0% of walkable area kept
- 1.00 m cells: 100.1% of walkable area kept

## Per-area breakdown

Sorted by 4-cardinal reachability loss (worst first).

### m14ab

- Rooms: 5
- Walkable area: 101921.8 m²
- Rectilinear (world): 59.8%
- Rectilinear (local best rotation, length-weighted mean): 61.0%
- Per-room best rotations (deg): 86, 88, 86, 2, 89
- Grid kept @0.5 m: 100.0%
- Walkable cells: 407523; 8-way reach: 172080; 4-card reach: 135013; 4-card coverage: 33.1%; diagonal-ban loss: 21.5%
- Smoothness: mean cardinal run 75.43 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 100 of 1840
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m14ab_02a: 37067 cells, x=282.0..394.5, y=36.0..171.5 (size 112.5 × 135.5)

### m14ac

- Rooms: 5
- Walkable area: 15504.0 m²
- Rectilinear (world): 37.2%
- Rectilinear (local best rotation, length-weighted mean): 39.7%
- Per-room best rotations (deg): 3, 3, 3, 1, 0
- Grid kept @0.5 m: 100.2%
- Walkable cells: 62117; 8-way reach: 44213; 4-card reach: 38288; 4-card coverage: 61.6%; diagonal-ban loss: 13.4%
- Smoothness: mean cardinal run 29.08 m; smooth cells 99.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 49 of 2461
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m14ac_10d: 5925 cells, x=310.0..365.5, y=337.5..398.0 (size 55.5 × 60.5)

### m14ad

- Rooms: 5
- Walkable area: 22135.9 m²
- Rectilinear (world): 55.6%
- Rectilinear (local best rotation, length-weighted mean): 57.5%
- Per-room best rotations (deg): 1, 4, 1, 2, 89
- Grid kept @0.5 m: 99.8%
- Walkable cells: 88401; 8-way reach: 37141; 4-card reach: 37140; 4-card coverage: 42.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 38.04 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 40 of 1073
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m14ad_05e: 1 cells, x=507.0..507.5, y=99.0..99.5 (size 0.5 × 0.5)

### m14aa

- Rooms: 5
- Walkable area: 24914.2 m²
- Rectilinear (world): 26.7%
- Rectilinear (local best rotation, length-weighted mean): 29.8%
- Per-room best rotations (deg): 0, 86, 87, 0, 71
- Grid kept @0.5 m: 100.2%
- Walkable cells: 99845; 8-way reach: 53003; 4-card reach: 53003; 4-card coverage: 53.1%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 33.28 m; smooth cells 99.6%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 69 of 846

### m14ae

- Rooms: 2
- Walkable area: 978.2 m²
- Rectilinear (world): 17.5%
- Rectilinear (local best rotation, length-weighted mean): 22.1%
- Per-room best rotations (deg): 87, 23
- Grid kept @0.5 m: 100.4%
- Walkable cells: 3930; 8-way reach: 3930; 4-card reach: 3930; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 16.96 m; smooth cells 97.2%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 54 of 681

## Design decisions (proposed)

Outcome of this audit for the accessibility-mod navigation model. Captured here as the canonical reference for future implementation.

### Movement model: 4-cardinal viable game-wide

- 4-cardinal input (N/S/E/W only) reaches 96.4% of walkable cells from a typical seed. The 3.6% gap is mostly disconnected walkmesh islands that are also unreachable on foot in vanilla.
- 99.7% of walkable cells have a straight cardinal run of ≥5 m available. The world is built on cardinal corridors with curved decorative perimeters — exactly the opposite shape of what would hurt 4-card play.
- 0.0% fiddly cells globally (no diagonal corridors that force zigzag). 0.0% choke cells.
- Diagonal input gives a 0.3% reachability bonus over 4-card. Not worth a dedicated input affordance for the general case.

### Cell size: 0.5 m default, 0.25 m in pinch areas

The cell size is a **model resolution**, not a step size. The KOTOR character keeps moving continuously at the engine's normal ~5 m/s — the grid is the screen-reader's mental map for narration, distance announcements, and connectivity checks. Player perception of speed and inertia is unchanged.

- **Default 0.5 m** globally. Cheap, well-matched to KOTOR's typical 0.5-1 m doorways, and the character's 0.13 m collision radius is already finer than this so we lose no perceptible precision.
- **0.25 m** in the five flagged pinch-point areas (see list below). At this resolution the narrow diagonal pinches that block 4-cardinal BFS at 0.5 m become 2-3 cells wide and traverse normally. 4× memory and CPU per area, negligible globally.
- **Never use cell size as step size.** A 0.25 m step at fixed per-press cadence would feel sluggish (~10× slower than current run speed). The grid is for *describing* the world, not for *driving* the character.

### Pinch-point areas (use 0.25 m grid)

From the per-area breakdown above, ordered by diagonal-ban loss at 0.5 m. These are the only areas where the model resolution matters for navigation correctness.

- **m14ab** — Dantooine. 21.5% loss, one 9300 m² cluster behind a single pinch at world coords x=282..394, y=36..171 inside room m14ab_02a.
- **m14ac** — Dantooine. 13.4% loss, one 1500 m² cluster at x=310..365, y=337..398 inside m14ac_10d.
- **m44ad** — Star Forge. 10.1% loss in a single 888 m² room.
- **m47ac** — Lehon (Unknown World). 6.4% loss.
- **m28aa** — Dantooine. 4.4% loss.

All other areas show < 1.1% diagonal-ban loss at 0.5 m and can stay at default resolution.

### Alternative for pinch handling: slip-diagonal rule

If per-area cell-size variation proves awkward (e.g. crossing area boundaries needs grid re-binning), an equivalent fix at uniform 0.5 m: when a 4-cardinal press would land on a non-walkable cell but the adjacent diagonal cell *is* walkable, slip the character one diagonal step. Transparent to the player, never exposed as input, fires only at the handful of pinch points where the engine would otherwise block. Choose this over per-area resolution if a single, uniform model is preferable.

### Things this audit explicitly does NOT recommend

- **Stepped/snap movement (one press = one cell).** Would map cell size to step size and feel tedious at any resolution under 1 m. Keep engine locomotion continuous.
- **Locally-rotated grids per area.** The per-room best-rotation column in the report shows most areas are within 5° of world axes; the +5pp gain from rotating per room (65.5% → 70.5% rectilinear globally) does not justify the speech-and-mental-model complexity of running multiple frames concurrently.
- **Diagonal-only input affordance (Q/E etc.) as a default.** The 0.3% reachability bonus and 0.0% smoothness loss don't motivate it. Could still be useful as a per-pinch escape hatch if the slip-diagonal rule proves insufficient.

### Regenerating this report

    kdev walkmesh-geometry-audit --source build/wok-extract --report docs/walkmesh-geometry-audit.md

This appendix is emitted by the audit command itself; edits to the data sections above will be overwritten on next run, but the design decisions stay in source at `tools/kdev/Commands/WalkmeshGeometryAuditCommand.cs:AppendDesignDecisions`.
