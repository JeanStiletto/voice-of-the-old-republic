// wall probing — "is there a wall, and how far?"
//
// This is the wall system, distinct from the room-shape system that reads
// it. Everything here is a single-ray or multi-ray cast against the cached
// perimeter walls (acc::spatial::change_detector::GetCachedWalls); nothing
// here knows about nav-graph nodes, clusters, or the Korridor / Kreuzung /
// Bereich vocabulary those produce.
//
// The two systems' names got entangled during development: the probes
// lived in a file called wall_topology.cpp which was, in substance, room
// topology. The Phase-1 structure pass (refactoring candidate 12) split
// them by name - the probes became this file, and the rest became
// room_topology.* on the consumer side.
//
// Probes fail open: with no wall cache, ProbeDistance returns -1 and
// IsAlcoveAlongAxis returns true, so a caller can never be blocked by
// missing geometry.

#pragma once

struct Vector;

namespace acc::engine { struct WallEdge; }

namespace acc::wall_probe {

// Resolve the height a probe from `pos` should cast at. Wall rays only
// see walls within the walkmesh z-guard of their own height, and many
// callers (nav-graph nodes, cluster centroids) carry z=0 — on any floor
// above the guard those rays fly under the whole map. Votes on the wall
// endpoints around (x,y) and returns the dominant height band; keeps
// pos.z untouched when it already sees that band (engine-sourced
// positions are correct and must not be dragged onto another storey).
// Every entry point below applies this itself; callers only need it
// directly when they log the resolved height.
float ResolveProbeZ(const acc::engine::WallEdge* walls, int wallCount,
                    const Vector& pos);

// Distance to the first wall along (dx,dy) from `pos`. Returns the probe
// range when the ray clears it, -1.0 when the wall cache is empty.
float ProbeDistance(const Vector& pos, float dx, float dy);

// Dead-end shape gate: a 4-ray probe rotated to align with
// (forwardX,forwardY) - the direction from a degree-1 node toward its
// graph parent, i.e. the way in. True when the forward ray is open while
// the position is boxed in on the other three sides (tight alcove or
// corridor terminus). Fails open when the wall cache isn't ready.
bool IsAlcoveAlongAxis(const Vector& pos, float forwardX, float forwardY);

// 8-ray clearance probe, filling outRays[8] in octant order
// (E, NE, N, NW, W, SW, S, SE). Each entry is the distance to the first
// wall hit, or the probe range when the ray clears it.
void ProbeClearance8(const acc::engine::WallEdge* walls, int wallCount,
                     const Vector& pos, float outRays[8]);

}  // namespace acc::wall_probe
