# Walkmesh geometry audit

Generated: 2026-05-11 07:42 UTC
Source: build/wok-extract/*.wok — area room walkmeshes only.
Cell size for reachability: 0.50 m.

## Headline numbers

- Areas analysed: 101
- Rooms analysed: 978
- Walkable triangles: 95590
- Total walkable area: 34506444.0 m²
- Total perimeter length: 182426.8 m
- Rectilinear fraction, world axes: 65.5%
- Rectilinear fraction, per-room best rotation: 70.5%
- Total walkable cells at 0.50 m: 14024821 (3506205 m²)
- 8-way reachable from seed: 13567532 (3391883 m²)
- 4-cardinal reachable from seed: 13522469 (3380617 m²)
- 4-cardinal coverage of walkable cells: 96.4%
- Cells uniquely unlocked by diagonals: 0.3% (4-card vs 8-way from same seed)
- Mean longest cardinal run per cell: 1641.12 m
- Mean longest diagonal run per cell: 100.38 m
- Smooth cells (≥5 m cardinal run): 99.7%
- Fiddly cells (cardinal ≤2 m, diagonal ≥5 m → forced zigzag in 4-card): 0.0%
- Choke cells (cardinal ≤1 m & diagonal ≤1 m → single-cell pinch): 0.0%
- Skinny triangles (min angle < 10°): 9657 of 95590 (10.1%)

## Edge-angle histogram (world axes)

Perimeter-edge length per 5° bin, folded into [0°, 90°). Cardinal bins (0-5° and 85-90°) measure axis-alignment under the world frame.

-  0- 5°: 27.9%  `############################`
-  5-10°: 2.8%  `###`
- 10-15°: 2.3%  `##`
- 15-20°: 2.1%  `##`
- 20-25°: 2.1%  `##`
- 25-30°: 1.8%  `##`
- 30-35°: 2.1%  `##`
- 35-40°: 1.8%  `##`
- 40-45°: 2.7%  `###`
- 45-50°: 3.3%  `###`
- 50-55°: 1.9%  `##`
- 55-60°: 1.9%  `##`
- 60-65°: 1.7%  `##`
- 65-70°: 1.8%  `##`
- 70-75°: 1.9%  `##`
- 75-80°: 2.0%  `##`
- 80-85°: 2.4%  `##`
- 85-90°: 37.6%  `######################################`

## Grid rasterisation loss (centre-walkable rule)

- 0.25 m cells: 6.8% of walkable area kept
- 0.50 m cells: 10.2% of walkable area kept
- 1.00 m cells: 10.2% of walkable area kept

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

### m44ad

- Rooms: 1
- Walkable area: 888.8 m²
- Rectilinear (world): 45.1%
- Rectilinear (local best rotation, length-weighted mean): 46.3%
- Per-room best rotations (deg): 1
- Grid kept @0.5 m: 100.4%
- Walkable cells: 3568; 8-way reach: 3568; 4-card reach: 3208; 4-card coverage: 89.9%; diagonal-ban loss: 10.1%
- Smoothness: mean cardinal run 26.15 m; smooth cells 99.9%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 29 of 100
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m44ad_01a: 360 cells, x=133.5..149.5, y=68.0..75.0 (size 16.0 × 7.0)

### m47ac

- Rooms: 1
- Walkable area: 1406.3 m²
- Rectilinear (world): 51.4%
- Rectilinear (local best rotation, length-weighted mean): 51.8%
- Per-room best rotations (deg): 1
- Grid kept @0.5 m: 100.5%
- Walkable cells: 5653; 8-way reach: 5653; 4-card reach: 5293; 4-card coverage: 93.6%; diagonal-ban loss: 6.4%
- Smoothness: mean cardinal run 29.29 m; smooth cells 99.7%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 91 of 241
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m47ac_01a: 360 cells, x=133.5..149.5, y=68.0..75.0 (size 16.0 × 7.0)

### m28aa

- Rooms: 22
- Walkable area: 7875.1 m²
- Rectilinear (world): 74.3%
- Rectilinear (local best rotation, length-weighted mean): 76.4%
- Per-room best rotations (deg): 0, 0, 2, 86, 0, 89, 1, 0, 0, 4, 4, 3, 86, 86, 0, 3, 86, 0, 0, 2, 2, 0
- Grid kept @0.5 m: 100.2%
- Walkable cells: 31571; 8-way reach: 26518; 4-card reach: 25358; 4-card coverage: 80.3%; diagonal-ban loss: 4.4%
- Smoothness: mean cardinal run 22.66 m; smooth cells 99.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 311 of 1124
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m28aa_11a: 1160 cells, x=150.0..167.0, y=55.5..84.0 (size 17.0 × 28.5)

### m03ae

- Rooms: 9
- Walkable area: 1290.6 m²
- Rectilinear (world): 55.9%
- Rectilinear (local best rotation, length-weighted mean): 60.1%
- Per-room best rotations (deg): 3, 89, 0, 4, 0, 86, 4, 42, 49
- Grid kept @0.5 m: 100.2%
- Walkable cells: 5175; 8-way reach: 5045; 4-card reach: 4991; 4-card coverage: 96.4%; diagonal-ban loss: 1.1%
- Smoothness: mean cardinal run 11.02 m; smooth cells 90.7%; fiddly cells 0.4%; choke cells 0.2%
- Skinny triangles (<10°): 104 of 504
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m03ae_04a: 48 cells, x=80.5..85.0, y=108.5..111.5 (size 4.5 × 3.0)
    - m03ae_12a: 5 cells, x=117.0..118.5, y=117.0..119.0 (size 1.5 × 2.0)
    - m03ae_09a: 1 cells, x=107.0..107.5, y=106.5..107.0 (size 0.5 × 0.5)

### m02ae

- Rooms: 10
- Walkable area: 1988.9 m²
- Rectilinear (world): 52.1%
- Rectilinear (local best rotation, length-weighted mean): 57.5%
- Per-room best rotations (deg): 2, 2, 2, 3, 0, 4, 43, 44, 42, 45
- Grid kept @0.5 m: 100.2%
- Walkable cells: 7970; 8-way reach: 7838; 4-card reach: 7760; 4-card coverage: 97.4%; diagonal-ban loss: 1.0%
- Smoothness: mean cardinal run 15.44 m; smooth cells 93.6%; fiddly cells 0.4%; choke cells 0.1%
- Skinny triangles (<10°): 126 of 544
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m02ae_04a: 48 cells, x=80.5..85.0, y=108.5..111.5 (size 4.5 × 3.0)
    - m02ae_03a: 12 cells, x=100.5..102.0, y=87.0..89.0 (size 1.5 × 2.0)
    - m02ae_01a: 8 cells, x=126.5..129.0, y=115.0..116.0 (size 2.5 × 1.0)
    - m02ae_12a: 5 cells, x=117.0..118.5, y=117.0..119.0 (size 1.5 × 2.0)
    - m02ae_10a: 3 cells, x=101.5..102.5, y=101.5..103.0 (size 1.0 × 1.5)
    - m02ae_10a: 1 cells, x=107.0..107.5, y=106.5..107.0 (size 0.5 × 0.5)
    - m02ae_11a: 1 cells, x=105.0..105.5, y=108.0..108.5 (size 0.5 × 0.5)

### m02af

- Rooms: 1
- Walkable area: 217.5 m²
- Rectilinear (world): 6.0%
- Rectilinear (local best rotation, length-weighted mean): 73.3%
- Per-room best rotations (deg): 41
- Grid kept @0.5 m: 99.1%
- Walkable cells: 862; 8-way reach: 862; 4-card reach: 858; 4-card coverage: 99.5%; diagonal-ban loss: 0.5%
- Smoothness: mean cardinal run 10.85 m; smooth cells 97.6%; fiddly cells 0.6%; choke cells 0.1%
- Skinny triangles (<10°): 8 of 46
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m02af_01a: 2 cells, x=81.0..82.0, y=130.5..131.0 (size 1.0 × 0.5)
    - m02af_01a: 1 cells, x=94.0..94.5, y=139.0..139.5 (size 0.5 × 0.5)
    - m02af_01a: 1 cells, x=82.5..83.0, y=131.5..132.0 (size 0.5 × 0.5)

### m02ad

- Rooms: 14
- Walkable area: 2012.3 m²
- Rectilinear (world): 16.3%
- Rectilinear (local best rotation, length-weighted mean): 54.5%
- Per-room best rotations (deg): 46, 45, 44, 0, 44, 3, 7, 1, 7, 1, 7, 53, 7, 53
- Grid kept @0.5 m: 100.6%
- Walkable cells: 8098; 8-way reach: 8098; 4-card reach: 8083; 4-card coverage: 99.8%; diagonal-ban loss: 0.2%
- Smoothness: mean cardinal run 10.35 m; smooth cells 96.9%; fiddly cells 0.6%; choke cells 0.0%
- Skinny triangles (<10°): 45 of 288
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m02ad_03a: 12 cells, x=88.5..91.0, y=142.0..144.5 (size 2.5 × 2.5)
    - m02ad_03a: 2 cells, x=85.5..86.5, y=131.0..131.5 (size 1.0 × 0.5)
    - m02ad_02a: 1 cells, x=143.0..143.5, y=144.5..145.0 (size 0.5 × 0.5)

### m17af

- Rooms: 1
- Walkable area: 267.3 m²
- Rectilinear (world): 61.8%
- Rectilinear (local best rotation, length-weighted mean): 63.5%
- Per-room best rotations (deg): 4
- Grid kept @0.5 m: 101.9%
- Walkable cells: 1090; 8-way reach: 989; 4-card reach: 988; 4-card coverage: 90.6%; diagonal-ban loss: 0.1%
- Smoothness: mean cardinal run 10.75 m; smooth cells 86.1%; fiddly cells 0.6%; choke cells 0.1%
- Skinny triangles (<10°): 53 of 455
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m17af_00a: 1 cells, x=3.5..4.0, y=8.5..9.0 (size 0.5 × 0.5)

### m03ab

- Rooms: 14
- Walkable area: 1996.7 m²
- Rectilinear (world): 16.0%
- Rectilinear (local best rotation, length-weighted mean): 55.3%
- Per-room best rotations (deg): 45, 46, 45, 0, 48, 3, 7, 1, 7, 1, 7, 53, 7, 53
- Grid kept @0.5 m: 100.3%
- Walkable cells: 8008; 8-way reach: 8007; 4-card reach: 8003; 4-card coverage: 99.9%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 10.52 m; smooth cells 98.2%; fiddly cells 0.3%; choke cells 0.0%
- Skinny triangles (<10°): 47 of 252
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m03ab_03a: 2 cells, x=83.5..84.5, y=147.5..148.5 (size 1.0 × 1.0)
    - m03ab_03a: 1 cells, x=84.0..84.5, y=135.0..135.5 (size 0.5 × 0.5)
    - m03ab_05a: 1 cells, x=87.5..88.0, y=79.5..80.0 (size 0.5 × 0.5)

### m03ad

- Rooms: 14
- Walkable area: 2006.9 m²
- Rectilinear (world): 16.1%
- Rectilinear (local best rotation, length-weighted mean): 53.6%
- Per-room best rotations (deg): 43, 44, 45, 1, 46, 4, 7, 1, 7, 1, 7, 53, 7, 53
- Grid kept @0.5 m: 100.8%
- Walkable cells: 8089; 8-way reach: 8089; 4-card reach: 8086; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 10.65 m; smooth cells 98.6%; fiddly cells 0.4%; choke cells 0.0%
- Skinny triangles (<10°): 41 of 235
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m03ad_02a: 2 cells, x=141.0..142.0, y=133.0..134.0 (size 1.0 × 1.0)
    - m03ad_03a: 1 cells, x=83.5..84.0, y=134.5..135.0 (size 0.5 × 0.5)

### m02aa

- Rooms: 15
- Walkable area: 1838.4 m²
- Rectilinear (world): 19.3%
- Rectilinear (local best rotation, length-weighted mean): 55.4%
- Per-room best rotations (deg): 46, 41, 41, 0, 43, 0, 0, 46, 1, 7, 53, 7, 53, 7, 53
- Grid kept @0.5 m: 100.4%
- Walkable cells: 7383; 8-way reach: 7383; 4-card reach: 7381; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 10.29 m; smooth cells 95.4%; fiddly cells 0.5%; choke cells 0.0%
- Skinny triangles (<10°): 37 of 231
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m02aa_02a: 1 cells, x=141.0..141.5, y=133.5..134.0 (size 0.5 × 0.5)
    - m02aa_02a: 1 cells, x=142.5..143.0, y=144.0..144.5 (size 0.5 × 0.5)

### m41ab

- Rooms: 4
- Walkable area: 6926.5 m²
- Rectilinear (world): 11.0%
- Rectilinear (local best rotation, length-weighted mean): 23.3%
- Per-room best rotations (deg): 77, 57, 48, 75
- Grid kept @0.5 m: 100.0%
- Walkable cells: 27695; 8-way reach: 27695; 4-card reach: 27690; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 30.06 m; smooth cells 99.1%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 105 of 905
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m41ab_06a: 2 cells, x=78.0..79.0, y=102.5..103.0 (size 1.0 × 0.5)
    - m41ab_06a: 2 cells, x=30.5..31.5, y=157.5..158.0 (size 1.0 × 0.5)
    - m41ab_06a: 1 cells, x=33.0..33.5, y=111.5..112.0 (size 0.5 × 0.5)

### m28ac

- Rooms: 9
- Walkable area: 1648.4 m²
- Rectilinear (world): 62.5%
- Rectilinear (local best rotation, length-weighted mean): 67.7%
- Per-room best rotations (deg): 1, 0, 0, 0, 0, 2, 1, 3, 66
- Grid kept @0.5 m: 100.4%
- Walkable cells: 6617; 8-way reach: 5894; 4-card reach: 5893; 4-card coverage: 89.1%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.92 m; smooth cells 98.7%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 45 of 266
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m28ac_23c: 1 cells, x=234.0..234.5, y=190.5..191.0 (size 0.5 × 0.5)

### m41aa

- Rooms: 6
- Walkable area: 8334.3 m²
- Rectilinear (world): 16.1%
- Rectilinear (local best rotation, length-weighted mean): 23.4%
- Per-room best rotations (deg): 86, 82, 3, 29, 13, 69
- Grid kept @0.5 m: 100.1%
- Walkable cells: 33357; 8-way reach: 33250; 4-card reach: 33246; 4-card coverage: 99.7%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 33.36 m; smooth cells 98.5%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 96 of 1364
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m41aa_01b: 2 cells, x=111.0..112.0, y=185.5..186.0 (size 1.0 × 0.5)
    - m41aa_02a: 1 cells, x=163.5..164.0, y=219.5..220.0 (size 0.5 × 0.5)
    - m41aa_02a: 1 cells, x=129.5..130.0, y=247.0..247.5 (size 0.5 × 0.5)

### m45aa

- Rooms: 13
- Walkable area: 4954.4 m²
- Rectilinear (world): 65.0%
- Rectilinear (local best rotation, length-weighted mean): 78.7%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 0, 4, 82, 8, 0, 0
- Grid kept @0.5 m: 99.4%
- Walkable cells: 19701; 8-way reach: 19701; 4-card reach: 19699; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 22.67 m; smooth cells 97.3%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 141 of 394
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m45aa_04a: 1 cells, x=-18.0..-17.5, y=-73.5..-73.0 (size 0.5 × 0.5)
    - m45aa_04b: 1 cells, x=154.5..155.0, y=-73.5..-73.0 (size 0.5 × 0.5)

### m22aa

- Rooms: 4
- Walkable area: 2634.8 m²
- Rectilinear (world): 25.9%
- Rectilinear (local best rotation, length-weighted mean): 42.1%
- Per-room best rotations (deg): 71, 12, 17, 0
- Grid kept @0.5 m: 99.9%
- Walkable cells: 10525; 8-way reach: 10525; 4-card reach: 10524; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.44 m; smooth cells 99.4%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 28 of 171
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m22aa_03a: 1 cells, x=172.0..172.5, y=71.5..72.0 (size 0.5 × 0.5)

### m41ac

- Rooms: 4
- Walkable area: 5473.0 m²
- Rectilinear (world): 14.8%
- Rectilinear (local best rotation, length-weighted mean): 20.4%
- Per-room best rotations (deg): 68, 87, 71, 1
- Grid kept @0.5 m: 100.0%
- Walkable cells: 21897; 8-way reach: 21410; 4-card reach: 21408; 4-card coverage: 97.8%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 27.05 m; smooth cells 98.4%; fiddly cells 0.2%; choke cells 0.0%
- Skinny triangles (<10°): 98 of 868
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m41ac_09b: 1 cells, x=174.5..175.0, y=171.0..171.5 (size 0.5 × 0.5)
    - m41ac_09c: 1 cells, x=179.0..179.5, y=71.5..72.0 (size 0.5 × 0.5)

### m23aa

- Rooms: 4
- Walkable area: 2824.5 m²
- Rectilinear (world): 10.7%
- Rectilinear (local best rotation, length-weighted mean): 32.8%
- Per-room best rotations (deg): 82, 39, 77, 61
- Grid kept @0.5 m: 100.0%
- Walkable cells: 11300; 8-way reach: 11300; 4-card reach: 11299; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 16.50 m; smooth cells 99.5%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 53 of 244
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m23aa_03a: 1 cells, x=78.5..79.0, y=93.5..94.0 (size 0.5 × 0.5)

### m26ab

- Rooms: 16
- Walkable area: 3223.7 m²
- Rectilinear (world): 67.8%
- Rectilinear (local best rotation, length-weighted mean): 76.0%
- Per-room best rotations (deg): 87, 0, 86, 43, 0, 0, 87, 0, 0, 3, 0, 0, 0, 0, 88, 0
- Grid kept @0.5 m: 99.9%
- Walkable cells: 12882; 8-way reach: 12585; 4-card reach: 12584; 4-card coverage: 97.7%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.34 m; smooth cells 98.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 138 of 363
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m26ab_15c: 1 cells, x=-37.0..-36.5, y=67.5..68.0 (size 0.5 × 0.5)

### m26ac

- Rooms: 11
- Walkable area: 3439.9 m²
- Rectilinear (world): 65.0%
- Rectilinear (local best rotation, length-weighted mean): 67.7%
- Per-room best rotations (deg): 0, 0, 0, 41, 0, 88, 88, 86, 0, 3, 41
- Grid kept @0.5 m: 99.4%
- Walkable cells: 13684; 8-way reach: 13657; 4-card reach: 13656; 4-card coverage: 99.8%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.18 m; smooth cells 99.3%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 133 of 488
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m26ac_01e: 1 cells, x=-56.5..-56.0, y=21.5..22.0 (size 0.5 × 0.5)

### m42aa

- Rooms: 12
- Walkable area: 4024.6 m²
- Rectilinear (world): 41.1%
- Rectilinear (local best rotation, length-weighted mean): 43.9%
- Per-room best rotations (deg): 0, 0, 0, 15, 0, 87, 0, 87, 0, 0, 0, 0
- Grid kept @0.5 m: 99.4%
- Walkable cells: 15999; 8-way reach: 15825; 4-card reach: 15824; 4-card coverage: 98.9%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 16.64 m; smooth cells 98.3%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 51 of 348
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m42aa_02a: 1 cells, x=49.0..49.5, y=67.5..68.0 (size 0.5 × 0.5)

### m26ad

- Rooms: 10
- Walkable area: 9560.9 m²
- Rectilinear (world): 31.1%
- Rectilinear (local best rotation, length-weighted mean): 78.3%
- Per-room best rotations (deg): 85, 0, 84, 15, 5, 11, 1, 15, 3, 7
- Grid kept @0.5 m: 100.0%
- Walkable cells: 38251; 8-way reach: 37385; 4-card reach: 37383; 4-card coverage: 97.7%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 36.02 m; smooth cells 99.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 180 of 483
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m26ad_01a: 2 cells, x=225.5..226.5, y=138.5..139.0 (size 1.0 × 0.5)

### m08aa

- Rooms: 29
- Walkable area: 5945.9 m²
- Rectilinear (world): 88.2%
- Rectilinear (local best rotation, length-weighted mean): 88.8%
- Per-room best rotations (deg): 0, 0, 0, 0, 3, 0, 87, 86, 4, 87, 0, 0, 0, 0, 0, 0, 0, 88, 0, 86, 0, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 100.1%
- Walkable cells: 23813; 8-way reach: 20441; 4-card reach: 20440; 4-card coverage: 85.8%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 19.21 m; smooth cells 98.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 284 of 986
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m08aa_10a: 1 cells, x=72.5..73.0, y=46.0..46.5 (size 0.5 × 0.5)

### m34aa

- Rooms: 10
- Walkable area: 6156.5 m²
- Rectilinear (world): 25.1%
- Rectilinear (local best rotation, length-weighted mean): 30.8%
- Per-room best rotations (deg): 89, 36, 41, 22, 71, 1, 88, 7, 2, 83
- Grid kept @0.5 m: 100.1%
- Walkable cells: 24655; 8-way reach: 24655; 4-card reach: 24654; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 28.67 m; smooth cells 98.3%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 84 of 3292
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m34aa_05a: 1 cells, x=174.5..175.0, y=143.5..144.0 (size 0.5 × 0.5)

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

### m22ab

- Rooms: 8
- Walkable area: 10286.3 m²
- Rectilinear (world): 10.6%
- Rectilinear (local best rotation, length-weighted mean): 28.7%
- Per-room best rotations (deg): 77, 37, 80, 79, 38, 12, 72, 1
- Grid kept @0.5 m: 100.0%
- Walkable cells: 41160; 8-way reach: 41160; 4-card reach: 41159; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 29.68 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 18 of 205
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m22ab_05a: 1 cells, x=115.5..116.0, y=312.5..313.0 (size 0.5 × 0.5)

### m19aa

- Rooms: 18
- Walkable area: 10858.8 m²
- Rectilinear (world): 64.2%
- Rectilinear (local best rotation, length-weighted mean): 65.3%
- Per-room best rotations (deg): 0, 0, 1, 86, 0, 0, 0, 0, 1, 4, 3, 87, 87, 1, 86, 0, 3, 0
- Grid kept @0.5 m: 100.2%
- Walkable cells: 43530; 8-way reach: 43530; 4-card reach: 43529; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 26.77 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 67 of 1818
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m19aa_02g: 1 cells, x=216.0..216.5, y=66.5..67.0 (size 0.5 × 0.5)

### m24aa

- Rooms: 12
- Walkable area: 13378.8 m²
- Rectilinear (world): 14.6%
- Rectilinear (local best rotation, length-weighted mean): 27.9%
- Per-room best rotations (deg): 44, 77, 29, 58, 63, 85, 12, 53, 58, 35, 8, 85
- Grid kept @0.5 m: 100.0%
- Walkable cells: 53509; 8-way reach: 53509; 4-card reach: 53508; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 27.53 m; smooth cells 99.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 8 of 638
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m24aa_07a: 1 cells, x=254.5..255.0, y=213.5..214.0 (size 0.5 × 0.5)

### m18aa

- Rooms: 4
- Walkable area: 102451.7 m²
- Rectilinear (world): 30.0%
- Rectilinear (local best rotation, length-weighted mean): 33.9%
- Per-room best rotations (deg): 62, 47, 3, 2
- Grid kept @0.5 m: 100.0%
- Walkable cells: 409799; 8-way reach: 409799; 4-card reach: 409796; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 108.80 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 5 of 12789
- Top diagonal-locked clusters (8-connectivity, cell count + bbox m):
    - m18aa_04a: 2 cells, x=383.5..384.5, y=124.0..124.5 (size 1.0 × 0.5)
    - m18aa_01a: 1 cells, x=77.0..77.5, y=230.5..231.0 (size 0.5 × 0.5)

### m01aa

- Rooms: 14
- Walkable area: 1841.7 m²
- Rectilinear (world): 84.5%
- Rectilinear (local best rotation, length-weighted mean): 84.5%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 100.6%
- Walkable cells: 7413; 8-way reach: 7413; 4-card reach: 7413; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.23 m; smooth cells 96.5%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 132 of 540

### m01ab

- Rooms: 5
- Walkable area: 773.5 m²
- Rectilinear (world): 87.2%
- Rectilinear (local best rotation, length-weighted mean): 87.3%
- Per-room best rotations (deg): 0, 0, 0, 1, 2
- Grid kept @0.5 m: 100.8%
- Walkable cells: 3120; 8-way reach: 3120; 4-card reach: 3120; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 15.57 m; smooth cells 94.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 75 of 250

### m02ab

- Rooms: 9
- Walkable area: 6651.1 m²
- Rectilinear (world): 77.2%
- Rectilinear (local best rotation, length-weighted mean): 78.1%
- Per-room best rotations (deg): 87, 0, 0, 0, 0, 3, 0, 0, 0
- Grid kept @0.5 m: 100.6%
- Walkable cells: 26751; 8-way reach: 26711; 4-card reach: 26711; 4-card coverage: 99.9%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 38.07 m; smooth cells 99.6%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 80 of 397

### m02ac

- Rooms: 13
- Walkable area: 5415.8 m²
- Rectilinear (world): 82.2%
- Rectilinear (local best rotation, length-weighted mean): 82.5%
- Per-room best rotations (deg): 0, 0, 0, 0, 88, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 101.1%
- Walkable cells: 21908; 8-way reach: 21856; 4-card reach: 21856; 4-card coverage: 99.8%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 22.93 m; smooth cells 99.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 58 of 312

### m03aa

- Rooms: 11
- Walkable area: 6809.1 m²
- Rectilinear (world): 88.3%
- Rectilinear (local best rotation, length-weighted mean): 89.8%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 87, 0, 88, 86, 0
- Grid kept @0.5 m: 99.9%
- Walkable cells: 27207; 8-way reach: 20076; 4-card reach: 20076; 4-card coverage: 73.8%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 49.13 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 64 of 274

### m03af

- Rooms: 1
- Walkable area: 654.8 m²
- Rectilinear (world): 88.1%
- Rectilinear (local best rotation, length-weighted mean): 88.1%
- Per-room best rotations (deg): 86
- Grid kept @0.5 m: 99.8%
- Walkable cells: 2614; 8-way reach: 2614; 4-card reach: 2614; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 39.98 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 13 of 38

### m03mg

- Rooms: 1
- Walkable area: 4020.2 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 100.0%
- Walkable cells: 16080; 8-way reach: 16080; 4-card reach: 16080; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 53.59 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 0 of 2

### m04aa

- Rooms: 12
- Walkable area: 16320.0 m²
- Rectilinear (world): 26.3%
- Rectilinear (local best rotation, length-weighted mean): 33.5%
- Per-room best rotations (deg): 53, 49, 4, 49, 2, 4, 87, 3, 3, 0, 4, 86
- Grid kept @0.5 m: 100.2%
- Walkable cells: 65382; 8-way reach: 63107; 4-card reach: 63107; 4-card coverage: 96.5%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 29.75 m; smooth cells 99.5%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 135 of 958

### m05aa

- Rooms: 25
- Walkable area: 4453.8 m²
- Rectilinear (world): 62.9%
- Rectilinear (local best rotation, length-weighted mean): 69.5%
- Per-room best rotations (deg): 0, 86, 87, 87, 2, 41, 42, 0, 86, 41, 0, 0, 0, 0, 41, 0, 0, 86, 2, 0, 4, 0, 0, 0, 1
- Grid kept @0.5 m: 100.1%
- Walkable cells: 17830; 8-way reach: 17830; 4-card reach: 17830; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.70 m; smooth cells 95.3%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 141 of 684

### m05ab

- Rooms: 11
- Walkable area: 3177.2 m²
- Rectilinear (world): 60.8%
- Rectilinear (local best rotation, length-weighted mean): 70.8%
- Per-room best rotations (deg): 49, 0, 41, 0, 41, 0, 40, 0, 41, 0, 0
- Grid kept @0.5 m: 100.4%
- Walkable cells: 12766; 8-way reach: 12766; 4-card reach: 12766; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 23.71 m; smooth cells 95.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 87 of 332

### m09aa

- Rooms: 17
- Walkable area: 3453.9 m²
- Rectilinear (world): 66.4%
- Rectilinear (local best rotation, length-weighted mean): 68.2%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 2, 0, 0, 1, 0, 0, 86, 0, 0, 0, 0, 35
- Grid kept @0.5 m: 100.2%
- Walkable cells: 13840; 8-way reach: 13840; 4-card reach: 13840; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 16.77 m; smooth cells 97.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 191 of 629

### m09ab

- Rooms: 3
- Walkable area: 214.1 m²
- Rectilinear (world): 79.3%
- Rectilinear (local best rotation, length-weighted mean): 79.3%
- Per-room best rotations (deg): 0, 0, 0
- Grid kept @0.5 m: 99.1%
- Walkable cells: 849; 8-way reach: 849; 4-card reach: 849; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 8.95 m; smooth cells 92.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 12 of 46

### m10aa

- Rooms: 17
- Walkable area: 4055.6 m²
- Rectilinear (world): 92.7%
- Rectilinear (local best rotation, length-weighted mean): 93.2%
- Per-room best rotations (deg): 0, 0, 0, 0, 1, 0, 0, 0, 86, 2, 0, 0, 0, 0, 1, 0, 0
- Grid kept @0.5 m: 99.3%
- Walkable cells: 16102; 8-way reach: 15981; 4-card reach: 15981; 4-card coverage: 99.2%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 19.66 m; smooth cells 98.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 130 of 683

### m10ab

- Rooms: 16
- Walkable area: 3225.7 m²
- Rectilinear (world): 94.7%
- Rectilinear (local best rotation, length-weighted mean): 95.2%
- Per-room best rotations (deg): 0, 1, 0, 0, 0, 86, 0, 0, 0, 0, 0, 0, 86, 86, 0, 0
- Grid kept @0.5 m: 98.8%
- Walkable cells: 12747; 8-way reach: 12747; 4-card reach: 12747; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 22.49 m; smooth cells 99.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 234 of 559

### m10ac

- Rooms: 16
- Walkable area: 3406.7 m²
- Rectilinear (world): 96.2%
- Rectilinear (local best rotation, length-weighted mean): 96.4%
- Per-room best rotations (deg): 0, 0, 4, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 99.7%
- Walkable cells: 13585; 8-way reach: 13585; 4-card reach: 13585; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 20.05 m; smooth cells 98.4%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 94 of 508

### m11aa

- Rooms: 11
- Walkable area: 3519.0 m²
- Rectilinear (world): 91.0%
- Rectilinear (local best rotation, length-weighted mean): 91.6%
- Per-room best rotations (deg): 0, 87, 0, 0, 0, 1, 0, 0, 1, 0, 0
- Grid kept @0.5 m: 99.5%
- Walkable cells: 14006; 8-way reach: 14006; 4-card reach: 14006; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 21.01 m; smooth cells 99.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 150 of 447

### m11ab

- Rooms: 2
- Walkable area: 150.3 m²
- Rectilinear (world): 90.6%
- Rectilinear (local best rotation, length-weighted mean): 90.6%
- Per-room best rotations (deg): 0, 0
- Grid kept @0.5 m: 100.5%
- Walkable cells: 604; 8-way reach: 604; 4-card reach: 604; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 8.42 m; smooth cells 86.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 47 of 109

### m12aa

- Rooms: 15
- Walkable area: 1031.2 m²
- Rectilinear (world): 46.9%
- Rectilinear (local best rotation, length-weighted mean): 50.4%
- Per-room best rotations (deg): 87, 0, 88, 1, 0, 0, 0, 1, 85, 86, 0, 86, 3, 4, 0
- Grid kept @0.5 m: 99.3%
- Walkable cells: 4095; 8-way reach: 4095; 4-card reach: 4095; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 8.87 m; smooth cells 82.7%; fiddly cells 0.4%; choke cells 0.0%
- Skinny triangles (<10°): 94 of 387

### m12ab

- Rooms: 1
- Walkable area: 15499969.0 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 0.0%
- Walkable cells: 0; 8-way reach: 0; 4-card reach: 0; 4-card coverage: 0.0%; diagonal-ban loss: 0.0%
- Skinny triangles (<10°): 0 of 2

### m12ac

- Rooms: 1
- Walkable area: 15499969.0 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 0.0%
- Walkable cells: 0; 8-way reach: 0; 4-card reach: 0; 4-card coverage: 0.0%; diagonal-ban loss: 0.0%
- Skinny triangles (<10°): 0 of 2

### m13aa

- Rooms: 19
- Walkable area: 6685.6 m²
- Rectilinear (world): 64.8%
- Rectilinear (local best rotation, length-weighted mean): 67.7%
- Per-room best rotations (deg): 88, 26, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 87, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 99.8%
- Walkable cells: 26690; 8-way reach: 26690; 4-card reach: 26690; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 28.41 m; smooth cells 98.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 172 of 739

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

### m15aa

- Rooms: 1
- Walkable area: 2643.0 m²
- Rectilinear (world): 69.5%
- Rectilinear (local best rotation, length-weighted mean): 70.6%
- Per-room best rotations (deg): 3
- Grid kept @0.5 m: 100.2%
- Walkable cells: 10591; 8-way reach: 10591; 4-card reach: 10591; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 35.30 m; smooth cells 99.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 97 of 232

### m16aa

- Rooms: 18
- Walkable area: 1952.8 m²
- Rectilinear (world): 76.9%
- Rectilinear (local best rotation, length-weighted mean): 78.6%
- Per-room best rotations (deg): 1, 87, 0, 3, 2, 0, 0, 3, 88, 0, 2, 89, 0, 1, 1, 0, 0, 0
- Grid kept @0.5 m: 100.8%
- Walkable cells: 7877; 8-way reach: 7877; 4-card reach: 7877; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 15.12 m; smooth cells 95.6%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 257 of 559

### m17aa

- Rooms: 26
- Walkable area: 6589.3 m²
- Rectilinear (world): 39.2%
- Rectilinear (local best rotation, length-weighted mean): 45.7%
- Per-room best rotations (deg): 44, 0, 28, 0, 1, 3, 87, 0, 0, 1, 1, 0, 0, 86, 9, 1, 0, 1, 4, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 99.9%
- Walkable cells: 26321; 8-way reach: 26275; 4-card reach: 26275; 4-card coverage: 99.8%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 20.39 m; smooth cells 98.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 270 of 653

### m17ab

- Rooms: 2
- Walkable area: 2114.7 m²
- Rectilinear (world): 64.8%
- Rectilinear (local best rotation, length-weighted mean): 65.7%
- Per-room best rotations (deg): 86, 0
- Grid kept @0.5 m: 99.8%
- Walkable cells: 8445; 8-way reach: 8445; 4-card reach: 8445; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 32.83 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 59 of 349

### m17ac

- Rooms: 2
- Walkable area: 214.5 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0, 0
- Grid kept @0.5 m: 102.0%
- Walkable cells: 875; 8-way reach: 875; 4-card reach: 875; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.56 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 19 of 283

### m17ad

- Rooms: 2
- Walkable area: 236.3 m²
- Rectilinear (world): 63.4%
- Rectilinear (local best rotation, length-weighted mean): 63.8%
- Per-room best rotations (deg): 87, 0
- Grid kept @0.5 m: 101.0%
- Walkable cells: 955; 8-way reach: 955; 4-card reach: 955; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.38 m; smooth cells 96.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 55 of 502

### m17ae

- Rooms: 1
- Walkable area: 246.5 m²
- Rectilinear (world): 82.8%
- Rectilinear (local best rotation, length-weighted mean): 82.8%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 103.5%
- Walkable cells: 1021; 8-way reach: 853; 4-card reach: 853; 4-card coverage: 83.5%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 11.48 m; smooth cells 97.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 35 of 201

### m17mg

- Rooms: 1
- Walkable area: 750464.1 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 100.0%
- Walkable cells: 3001920; 8-way reach: 3001920; 4-card reach: 3001920; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 3518.12 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 0 of 40

### m18ab

- Rooms: 3
- Walkable area: 77413.8 m²
- Rectilinear (world): 30.6%
- Rectilinear (local best rotation, length-weighted mean): 32.5%
- Per-room best rotations (deg): 2, 2, 2
- Grid kept @0.5 m: 100.0%
- Walkable cells: 309682; 8-way reach: 306890; 4-card reach: 306890; 4-card coverage: 99.1%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 173.97 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 8 of 18041

### m18ac

- Rooms: 2
- Walkable area: 43174.8 m²
- Rectilinear (world): 28.2%
- Rectilinear (local best rotation, length-weighted mean): 33.8%
- Per-room best rotations (deg): 87, 85
- Grid kept @0.5 m: 100.0%
- Walkable cells: 172719; 8-way reach: 172719; 4-card reach: 172719; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 131.31 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 5 of 12541

### m20aa

- Rooms: 9
- Walkable area: 1772.7 m²
- Rectilinear (world): 44.8%
- Rectilinear (local best rotation, length-weighted mean): 49.0%
- Per-room best rotations (deg): 4, 4, 83, 2, 62, 28, 89, 4, 0
- Grid kept @0.5 m: 99.9%
- Walkable cells: 7084; 8-way reach: 6043; 4-card reach: 6043; 4-card coverage: 85.3%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.22 m; smooth cells 99.5%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 71 of 1743

### m21aa

- Rooms: 11
- Walkable area: 2294.6 m²
- Rectilinear (world): 86.6%
- Rectilinear (local best rotation, length-weighted mean): 86.7%
- Per-room best rotations (deg): 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 99.6%
- Walkable cells: 9146; 8-way reach: 9146; 4-card reach: 9146; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 16.56 m; smooth cells 99.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 67 of 338

### m23ab

- Rooms: 1
- Walkable area: 203.5 m²
- Rectilinear (world): 14.0%
- Rectilinear (local best rotation, length-weighted mean): 23.9%
- Per-room best rotations (deg): 54
- Grid kept @0.5 m: 99.9%
- Walkable cells: 813; 8-way reach: 813; 4-card reach: 813; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.68 m; smooth cells 99.3%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 36 of 66

### m23ac

- Rooms: 1
- Walkable area: 210.2 m²
- Rectilinear (world): 17.2%
- Rectilinear (local best rotation, length-weighted mean): 34.3%
- Per-room best rotations (deg): 54
- Grid kept @0.5 m: 100.9%
- Walkable cells: 848; 8-way reach: 848; 4-card reach: 848; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 14.74 m; smooth cells 99.3%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 34 of 56

### m23ad

- Rooms: 1
- Walkable area: 525.8 m²
- Rectilinear (world): 11.5%
- Rectilinear (local best rotation, length-weighted mean): 18.4%
- Per-room best rotations (deg): 58
- Grid kept @0.5 m: 100.4%
- Walkable cells: 2111; 8-way reach: 2111; 4-card reach: 2111; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.86 m; smooth cells 98.2%; fiddly cells 0.2%; choke cells 0.0%
- Skinny triangles (<10°): 70 of 150

### m25aa

- Rooms: 12
- Walkable area: 13150.1 m²
- Rectilinear (world): 11.6%
- Rectilinear (local best rotation, length-weighted mean): 23.2%
- Per-room best rotations (deg): 67, 33, 17, 22, 0, 73, 46, 33, 82, 29, 60, 23
- Grid kept @0.5 m: 100.0%
- Walkable cells: 52609; 8-way reach: 52609; 4-card reach: 52609; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 24.17 m; smooth cells 99.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 358 of 1781

### m25ab

- Rooms: 10
- Walkable area: 8441.1 m²
- Rectilinear (world): 10.8%
- Rectilinear (local best rotation, length-weighted mean): 29.0%
- Per-room best rotations (deg): 87, 23, 40, 45, 42, 49, 83, 10, 6, 15
- Grid kept @0.5 m: 100.0%
- Walkable cells: 33774; 8-way reach: 33774; 4-card reach: 33774; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 22.43 m; smooth cells 99.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 14 of 827

### m26aa

- Rooms: 8
- Walkable area: 2191.7 m²
- Rectilinear (world): 33.5%
- Rectilinear (local best rotation, length-weighted mean): 53.3%
- Per-room best rotations (deg): 65, 3, 2, 0, 54, 52, 88, 43
- Grid kept @0.5 m: 100.3%
- Walkable cells: 8789; 8-way reach: 8662; 4-card reach: 8662; 4-card coverage: 98.6%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 15.00 m; smooth cells 96.1%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 152 of 413

### m26ae

- Rooms: 20
- Walkable area: 5053.5 m²
- Rectilinear (world): 73.4%
- Rectilinear (local best rotation, length-weighted mean): 75.6%
- Per-room best rotations (deg): 0, 0, 0, 41, 3, 0, 0, 0, 1, 0, 2, 42, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 100.1%
- Walkable cells: 20236; 8-way reach: 20114; 4-card reach: 20114; 4-card coverage: 99.4%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 18.24 m; smooth cells 98.5%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 223 of 637

### m26mg

- Rooms: 1
- Walkable area: 1834361.4 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 100.0%
- Walkable cells: 7334692; 8-way reach: 7334692; 4-card reach: 7334692; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Skinny triangles (<10°): 44 of 56

### m27aa

- Rooms: 34
- Walkable area: 6653.1 m²
- Rectilinear (world): 73.5%
- Rectilinear (local best rotation, length-weighted mean): 75.5%
- Per-room best rotations (deg): 3, 86, 4, 0, 5, 1, 89, 0, 86, 87, 86, 86, 86, 3, 86, 2, 0, 0, 0, 0, 0, 2, 0, 88, 0, 2, 87, 87, 0, 3, 2, 0, 86, 0
- Grid kept @0.5 m: 100.2%
- Walkable cells: 26674; 8-way reach: 26116; 4-card reach: 26116; 4-card coverage: 97.9%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 18.31 m; smooth cells 99.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 203 of 742

### m28ab

- Rooms: 18
- Walkable area: 13341.4 m²
- Rectilinear (world): 62.1%
- Rectilinear (local best rotation, length-weighted mean): 64.9%
- Per-room best rotations (deg): 87, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 86, 0, 0, 86, 2, 3, 8
- Grid kept @0.5 m: 99.9%
- Walkable cells: 53294; 8-way reach: 28455; 4-card reach: 28455; 4-card coverage: 53.4%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 52.28 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 127 of 624

### m28ad

- Rooms: 1
- Walkable area: 17031.2 m²
- Rectilinear (world): 56.6%
- Rectilinear (local best rotation, length-weighted mean): 59.9%
- Per-room best rotations (deg): 87
- Grid kept @0.5 m: 100.0%
- Walkable cells: 68117; 8-way reach: 21395; 4-card reach: 21395; 4-card coverage: 31.4%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 69.10 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 22 of 163

### m33aa

- Rooms: 7
- Walkable area: 3351.0 m²
- Rectilinear (world): 9.8%
- Rectilinear (local best rotation, length-weighted mean): 72.7%
- Per-room best rotations (deg): 3, 28, 29, 29, 32, 29, 29
- Grid kept @0.5 m: 100.0%
- Walkable cells: 13405; 8-way reach: 13287; 4-card reach: 13287; 4-card coverage: 99.1%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 15.13 m; smooth cells 96.1%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 61 of 384

### m33ab

- Rooms: 1
- Walkable area: 1007.6 m²
- Rectilinear (world): 41.1%
- Rectilinear (local best rotation, length-weighted mean): 55.6%
- Per-room best rotations (deg): 4
- Grid kept @0.5 m: 100.1%
- Walkable cells: 4035; 8-way reach: 4031; 4-card reach: 4031; 4-card coverage: 99.9%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 23.80 m; smooth cells 97.7%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 49 of 269

### m35aa

- Rooms: 23
- Walkable area: 4479.3 m²
- Rectilinear (world): 86.7%
- Rectilinear (local best rotation, length-weighted mean): 86.7%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 103.2%
- Walkable cells: 18497; 8-way reach: 18497; 4-card reach: 18497; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.12 m; smooth cells 99.5%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 251 of 562

### m36aa

- Rooms: 3
- Walkable area: 10425.7 m²
- Rectilinear (world): 27.8%
- Rectilinear (local best rotation, length-weighted mean): 33.1%
- Per-room best rotations (deg): 86, 87, 20
- Grid kept @0.5 m: 100.0%
- Walkable cells: 41684; 8-way reach: 41390; 4-card reach: 41390; 4-card coverage: 99.3%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 53.34 m; smooth cells 99.4%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 245 of 1248

### m37aa

- Rooms: 9
- Walkable area: 941.1 m²
- Rectilinear (world): 76.1%
- Rectilinear (local best rotation, length-weighted mean): 77.3%
- Per-room best rotations (deg): 88, 0, 0, 0, 0, 0, 1, 1, 87
- Grid kept @0.5 m: 102.9%
- Walkable cells: 3874; 8-way reach: 3874; 4-card reach: 3874; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.01 m; smooth cells 95.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 111 of 268

### m38aa

- Rooms: 11
- Walkable area: 1246.4 m²
- Rectilinear (world): 64.8%
- Rectilinear (local best rotation, length-weighted mean): 69.6%
- Per-room best rotations (deg): 59, 0, 86, 3, 5, 3, 88, 1, 2, 86, 2
- Grid kept @0.5 m: 100.8%
- Walkable cells: 5027; 8-way reach: 5027; 4-card reach: 5027; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 10.15 m; smooth cells 95.1%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 241 of 499

### m38ab

- Rooms: 8
- Walkable area: 921.9 m²
- Rectilinear (world): 63.7%
- Rectilinear (local best rotation, length-weighted mean): 67.5%
- Per-room best rotations (deg): 5, 4, 1, 5, 5, 2, 4, 1
- Grid kept @0.5 m: 100.0%
- Walkable cells: 3688; 8-way reach: 3688; 4-card reach: 3688; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 11.15 m; smooth cells 95.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 189 of 349

### m39aa

- Rooms: 16
- Walkable area: 2690.0 m²
- Rectilinear (world): 68.6%
- Rectilinear (local best rotation, length-weighted mean): 69.6%
- Per-room best rotations (deg): 0, 3, 3, 0, 0, 0, 88, 1, 0, 0, 1, 3, 5, 2, 1, 3
- Grid kept @0.5 m: 100.3%
- Walkable cells: 10790; 8-way reach: 10790; 4-card reach: 10790; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 16.79 m; smooth cells 98.6%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 357 of 665

### m40aa

- Rooms: 27
- Walkable area: 3184.2 m²
- Rectilinear (world): 92.0%
- Rectilinear (local best rotation, length-weighted mean): 92.2%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 100.5%
- Walkable cells: 12795; 8-way reach: 12795; 4-card reach: 12795; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 12.66 m; smooth cells 99.1%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 62 of 403

### m40ab

- Rooms: 26
- Walkable area: 4870.1 m²
- Rectilinear (world): 91.9%
- Rectilinear (local best rotation, length-weighted mean): 92.2%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 86, 0, 0, 0, 0, 4, 4
- Grid kept @0.5 m: 100.5%
- Walkable cells: 19585; 8-way reach: 19585; 4-card reach: 19585; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.19 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 112 of 531

### m40ac

- Rooms: 31
- Walkable area: 7596.7 m²
- Rectilinear (world): 92.4%
- Rectilinear (local best rotation, length-weighted mean): 92.6%
- Per-room best rotations (deg): 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
- Grid kept @0.5 m: 100.3%
- Walkable cells: 30475; 8-way reach: 27631; 4-card reach: 27631; 4-card coverage: 90.7%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 36.49 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 69 of 344

### m40ad

- Rooms: 11
- Walkable area: 2580.9 m²
- Rectilinear (world): 88.1%
- Rectilinear (local best rotation, length-weighted mean): 88.8%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4
- Grid kept @0.5 m: 100.2%
- Walkable cells: 10341; 8-way reach: 8131; 4-card reach: 8131; 4-card coverage: 78.6%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 17.55 m; smooth cells 99.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 68 of 254

### m41ad

- Rooms: 6
- Walkable area: 8903.5 m²
- Rectilinear (world): 24.0%
- Rectilinear (local best rotation, length-weighted mean): 32.9%
- Per-room best rotations (deg): 87, 2, 87, 3, 45, 86
- Grid kept @0.5 m: 99.9%
- Walkable cells: 35577; 8-way reach: 32711; 4-card reach: 32711; 4-card coverage: 91.9%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 26.47 m; smooth cells 99.6%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 183 of 876

### m41az

- Rooms: 1
- Walkable area: 162.4 m²
- Rectilinear (world): 89.7%
- Rectilinear (local best rotation, length-weighted mean): 89.7%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 95.0%
- Walkable cells: 617; 8-way reach: 617; 4-card reach: 617; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 18.15 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 3 of 15

### m43aa

- Rooms: 12
- Walkable area: 5867.7 m²
- Rectilinear (world): 44.9%
- Rectilinear (local best rotation, length-weighted mean): 56.6%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 45, 0, 1, 0, 0
- Grid kept @0.5 m: 100.0%
- Walkable cells: 23475; 8-way reach: 23475; 4-card reach: 23475; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 25.47 m; smooth cells 98.7%; fiddly cells 0.2%; choke cells 0.0%
- Skinny triangles (<10°): 99 of 392

### m44aa

- Rooms: 40
- Walkable area: 5005.3 m²
- Rectilinear (world): 67.3%
- Rectilinear (local best rotation, length-weighted mean): 70.0%
- Per-room best rotations (deg): 88, 1, 0, 86, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 86, 3, 0, 86, 87, 0, 3, 0, 4, 4, 0, 0, 86, 88, 0, 88, 86, 0, 0, 0, 0, 4, 2, 3, 3
- Grid kept @0.5 m: 100.6%
- Walkable cells: 20145; 8-way reach: 20145; 4-card reach: 20145; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 13.65 m; smooth cells 99.5%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 258 of 1423

### m44ab

- Rooms: 5
- Walkable area: 1035.7 m²
- Rectilinear (world): 66.6%
- Rectilinear (local best rotation, length-weighted mean): 70.3%
- Per-room best rotations (deg): 88, 3, 0, 0, 3
- Grid kept @0.5 m: 99.9%
- Walkable cells: 4138; 8-way reach: 4138; 4-card reach: 4138; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 15.16 m; smooth cells 98.9%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 55 of 226

### m44ac

- Rooms: 1
- Walkable area: 1414.6 m²
- Rectilinear (world): 48.1%
- Rectilinear (local best rotation, length-weighted mean): 48.7%
- Per-room best rotations (deg): 2
- Grid kept @0.5 m: 100.2%
- Walkable cells: 5671; 8-way reach: 5671; 4-card reach: 5671; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 33.89 m; smooth cells 99.7%; fiddly cells 0.1%; choke cells 0.0%
- Skinny triangles (<10°): 85 of 265

### m45ab

- Rooms: 15
- Walkable area: 4555.7 m²
- Rectilinear (world): 52.4%
- Rectilinear (local best rotation, length-weighted mean): 73.9%
- Per-room best rotations (deg): 0, 0, 0, 0, 0, 0, 0, 0, 86, 72, 70, 13, 12, 70, 20
- Grid kept @0.5 m: 99.9%
- Walkable cells: 18213; 8-way reach: 18213; 4-card reach: 18213; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 29.03 m; smooth cells 98.7%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 88 of 288

### m45ac

- Rooms: 14
- Walkable area: 8878.9 m²
- Rectilinear (world): 39.6%
- Rectilinear (local best rotation, length-weighted mean): 55.6%
- Per-room best rotations (deg): 86, 70, 72, 70, 20, 0, 0, 0, 0, 1, 0, 4, 9, 7
- Grid kept @0.5 m: 99.8%
- Walkable cells: 35428; 8-way reach: 35428; 4-card reach: 35428; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 29.73 m; smooth cells 99.5%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 97 of 490

### m45ad

- Rooms: 5
- Walkable area: 4300.7 m²
- Rectilinear (world): 38.6%
- Rectilinear (local best rotation, length-weighted mean): 44.7%
- Per-room best rotations (deg): 0, 0, 3, 41, 41
- Grid kept @0.5 m: 100.2%
- Walkable cells: 17238; 8-way reach: 17238; 4-card reach: 17238; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 31.66 m; smooth cells 98.6%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 47 of 232

### m45mg

- Rooms: 1
- Walkable area: 160000.0 m²
- Rectilinear (world): 100.0%
- Rectilinear (local best rotation, length-weighted mean): 100.0%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 100.0%
- Walkable cells: 640000; 8-way reach: 640000; 4-card reach: 640000; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 333.58 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 0 of 2

### m46aa

- Rooms: 1
- Walkable area: 6732.6 m²
- Rectilinear (world): 0.0%
- Rectilinear (local best rotation, length-weighted mean): 20.0%
- Per-room best rotations (deg): 41
- Grid kept @0.5 m: 99.9%
- Walkable cells: 26916; 8-way reach: 26916; 4-card reach: 26916; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 72.07 m; smooth cells 100.0%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 0 of 20

### m50aa

- Rooms: 1
- Walkable area: 848.9 m²
- Rectilinear (world): 94.7%
- Rectilinear (local best rotation, length-weighted mean): 94.7%
- Per-room best rotations (deg): 0
- Grid kept @0.5 m: 99.2%
- Walkable cells: 3368; 8-way reach: 3368; 4-card reach: 3368; 4-card coverage: 100.0%; diagonal-ban loss: 0.0%
- Smoothness: mean cardinal run 65.24 m; smooth cells 98.8%; fiddly cells 0.0%; choke cells 0.0%
- Skinny triangles (<10°): 54 of 147

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
