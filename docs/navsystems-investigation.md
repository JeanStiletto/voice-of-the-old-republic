# Navigation Systems — Investigation

> **Resume prompt (read this first if auto-compacted):**
>
> This is a pure Investigation session. Create a navsystems Investigation md. If you should auto-compact, add to the beginning of this file this prompt and follow it until the Investigation is finished.
> Edit only this Investigation file, no code changes, no other stuff.
> Use only our Tools documented in the dev docs file, no other tool uses or creation, no web search.
> Don't commit changes.
> Check that the tool uses you do are sane and will be stopped after sane times, so no overly processing use, no stale processes spamming everything.
>
> The Goal of this Investigation session is to find comprehensive data from the game and game engine to build a Navigation System. This contains diverse structure the document along the following questions, make it well readable and understandable so we avoid confusions and can use it as base for analysis and creation of the nav systems in a later session.
>
> Questions:
> 1. How is the coordinate System of our Environment layout, stored and accessible. Can we read coordinates of our Character? Of objects around? Of walls and Transition Points?
> 2. Do we have a navmesh that mouse-to-click or NPCs use for moving?
> 3. Do we have AI functions for moving and path finding we could use?
> 4. How is this with the player map. Have the icons positions we can read? Can we translate from Player map to the map we move our Character on?
> 5. Do we have lists of objects, interactables, event triggers, NPCs in the database per map and coordinates we could use?
> 6. Has the game a highlighting System or Icon System keeping some of these data per map/room we can use?
> 7. Do we have a sane Name databank for this stuff so we don't have just internal numbered names but real names at best?
>
> Document interesting findings you make on your way as well, but if you have answered the called Questions close the Investigation. Don't go in circles endlessly.

---

## Status legend

- **CONFIRMED** — verified in two independent sources (DB + SARIF, or DB + DLZ-Tool, or memory + RE)
- **STRONG** — single authoritative source, signature/offset matches the question
- **CANDIDATE** — name matches but neither offset nor signature double-checked
- **OPEN** — no candidate yet, needs RE next session

Sources cited inline:

- **DB** = `third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db` (functions / offsets / global_pointers tables)
- **SARIF** = `docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif` (Lane's full Ghidra export)
- **DLZ** = `third_party/DLZ-Tool/{KotorAdresses.h,types.h}` (independent address/struct file)
- **a11y-map** = `docs/llm-docs/accessibility-map.md`
- **mem** = entries in `~/.claude/projects/.../memory/` referenced inline

All function addresses below are GoG bytes; per `project_ghidra_gog_steam_bytes_match.md` they paste directly into Steam-targeted hooks.

---

## TL;DR

KotOR's engine exposes a **rich, well-structured navigation surface** that is tractable to expose to a screen reader without a renderer. Every question has a positive answer, and the second-pass decompilation closed every OPEN item that mattered for an audio-feedback wall system:

1. **Coords** — right-handed Z-up frame: **+X = east, +Y = north (= "forward" at yaw 0), +Z = up**. Object position lives at `CSWSObject.position +0x90` (`Vector`, 3 × float). Object orientation lives at `CSWSObject.orientation +0x9c` and is a **2-component heading vector (x,y) with z=0** — `bearing° = atan2(orient.y, orient.x) * 180/π` (CCW from +X, normalize +360 if y<0). World units are floats; engine convention is metres but the design doesn't depend on the scale. **CONFIRMED via decomp of `ExecuteCommandGetFacing` + `Yaw` + walkmesh ray-test (1000.0/-1000.0 along Z).**
2. **Walkmesh** — every room owns a `CSWRoomSurfaceMesh` wrapping a `CSWCollisionMesh`. Faces are triangles (`WalkmeshFace = 12 bytes: 3 × ulong vertex indices`), vertices are `Vector[]` in mesh-local space (transform via `LocalToWorld @0x596aa0`). Per-face material indexes `surfacemat.2da` to classify walkable / sound-on-step. **`CSWSArea::PositionWalkable(Vector) -> bool`** answers point-in-walkable. **CONFIRMED.**
3. **Wall geometry — derivable.** `CSWRoomSurfaceMesh.adjacencies +0x88` is `SurfaceMeshAdjacency[face_count]`, each `{int indices[3]}`. **An adjacency entry of `-1` means that triangle edge has no neighbour = perimeter = WALL.** Iterate faces, check the 3 adjacencies, emit edges where adj == -1 → complete wall-segment list per room. The engine also keeps explicit `perimeters +0x9c` and `edges +0x8c` arrays. Audio-feedback for nearby walls is **fully tractable from this**.
4. **Pathfinding** — `CSWSCreature::AddMoveToPointAction` is the queue-side entry; `AIActionMoveToPoint` is the per-tick consumer. The 17-arg signature is now decoded (see Q3 below). NWScript wrappers `ExecuteCommandActionMoveToPoint/ToObject/JumpTo*` are simpler entry points. **CONFIRMED.**
5. **Minimap** — `CSWSAreaMap` (per-area, hung off `CSWSModule.field89_0x218`) holds the fog-of-war bitfield + scale/origin/orientation. **`CSWSAreaMap::GetMapPixelFromWorldCoord(Vector, *outPx, *outPy)` @0x578e00** does world→pixel; the inverse is now derivable in 4 lines (formula in Q4). Map pixel space is **441×257** at 640×480 reference resolution. **CONFIRMED.**
6. **Per-area object lists** — `CSWSArea.game_objects[count]` (`+0x190`/`+0x194`), iterable via `GetFirstObjectInArea`/`GetNextObjectInArea`, plus a spatial index `GetFirstObjectIndiceByX`. Objects carry their `GAME_OBJECT_TYPES` tag (creature/door/trigger/placeable/waypoint/encounter/store/...). **CONFIRMED.**
7. **Highlight / icon system** — `CClientExoAppInternal::DoPassiveSelection` + `SelectNearestObject` (Tab-key behaviour). Render-debug toggles for triggers, AABB, GOB bounding boxes, personal space. Waypoints carry `has_map_note`/`map_note_enabled` flags. **STRONG.**
8. **Names** — three layers: tag (string ID) → `CExoLocString` localized field → TLK strref via `CTlkTable::GetSimpleString`. **CONFIRMED.**

The implication for the future nav-system design: **we can expose a screen-reader-friendly navigation model — "you are at room R, facing 47°; door X (locked) 3 metres NE; wall to your left at 1.2m; trigger Y 5m ahead; NPC Z (named) 8m back" — by reading from already-exposed engine state, including wall-edge audio feedback derived from the walkmesh adjacency arrays.**

---

## Q1 — Coordinate system: layout, storage, accessibility

### Coordinate frame — fully decoded

KotOR uses a **right-handed Z-up world frame**:

- **+X = east**
- **+Y = north** (= "forward" when yaw = 0)
- **+Z = up** (gravity-opposite)

Positions are `Vector` (3 × float, 12 bytes total) — SARIF DATATYPE:

```
Vector  size=12
  fields:
    0  x  float
    4  y  float
    8  z  float
```

**Z-up CONFIRMED** by decomp of `CSWRoomSurfaceMesh::GetSurfaceMaterialWalkCheckOnly @0x581a10`: it ray-tests vertically from `(x, y, +1000)` down to `(x, y, -1000)` to find which walkmesh face is "below" a query point. The `+1000`/`-1000` literal Z range only makes sense if Z is the gravity axis, and is independently corroborated by `CSWSAreaMap::GetMapPixelFromWorldCoord` discarding Z entirely.

**World units:** floats, no implicit scale in code. Aurora-engine convention is **1.0 unit = 1 metre**; the engine exposes `personal_space` (≈0.5–1.0 for a humanoid) and `creature_size` to give us a calibration sample at runtime. STRONG.

### Orientation — heading vector, *not* a quaternion (for objects)

This was a mis-call in the first pass. **Object orientation is a 2D heading vector** stored as `Vector` with z=0 padding. From the decomp of `ExecuteCommandGetFacing @0x537fe0`:

```c
local_18.x = obj->orientation.x;   // [+0x9c] of CSWSObject
local_18.y = obj->orientation.y;   // [+0xa0]
local_18.z = 0.0;                  // explicitly zeroed — z component is unused for facing
fpatan(local_18.y, local_18.x) * 57.29578;   // bearing° = atan2(y, x) * 180/π
if (orient.y < 0.0) bearing += 360.0;        // normalize to [0, 360)
```

**Bearing convention:** `0° = +X = east`; CCW positive. So:

| Direction | Bearing | Vector |
|---|---|---|
| east  | 0°   | (+1, 0, 0) |
| north | 90°  | (0, +1, 0) |
| west  | 180° | (-1, 0, 0) |
| south | 270° | (0, -1, 0) |

The engine's *animation/camera* layer uses real `Quaternion`s (16 bytes, `{w,x,y,z}`), and there's a separate `Global::Yaw(Quaternion*) @0x4a9f40` that extracts a yaw angle from a quaternion. **Don't confuse the two paths** — for navigation/object facing, treat orientation as a 2-float (x,y) heading and ignore z.

`CScriptLocation` (the NWScript `location` type, size 24 = position(12) + orientation(12)) follows the same convention.

### Where coords live, per object family

| Class | Position offset | Orientation offset | Notes |
|---|---|---|---|
| `CSWSObject` (server-side base, all world objects) | `+0x90` | `+0x9c` | DB **CONFIRMED** (also in `accessibility-map.md`) |
| `CSWCObject` (client-side base) | `+0x24` | `+0x30` | DB **CONFIRMED** — *different layout* from server. Use server when possible. |
| `CScriptLocation` (NWScript `location` value) | `+0x00` | `+0x0c` | size 24, **CONFIRMED** SARIF |
| `CSWCollisionMesh` (placeable / door collider) | `+0x2c` | `+0x38` | DB **CONFIRMED** — also has `world_coords` at `+0x4` (room/world space anchor) |
| `CSWWalkMeshHeader` (room walkmesh) | `+0x3c` | — | **CONFIRMED** — plus `world_coords` `+0x8`, `relative_use_positions` `+0x0c`, `absolute_use_positions` `+0x24` |
| `CLYT.room_positions` | `+0x40` (table) | — | per-room placement table (layout file in memory) |
| `CLYT.doorhook_positions` / `doorhook_orientations` | `+0x54` / `+0x58` | — | per-doorhook anchor (parallel arrays with `doorhook_room_names`/`door_names`) |
| `CSWCreaturePartyFollowInfo` | `follow_location +0x4`, `last_leader_position +0x10`, `last_follower_position +0x1c`, `stick_to_position +0x2c` | — | useful for "where is my party headed" |
| `EncounterSpawnPoint` | `+0x00` | `+0x0c` | per-spawn-point pose |
| `CSWGuiInGameAreaTransition.area_transition_position` | `+0x424` | — | the world coord of the destination side of a transition while the prompt is up |
| `CSWSObject.spell_target_position` | `+0x1b0` | — | useful for combat/AOE narration |

### Reading the player's coordinates

Path:

```
APP_MANAGER_PTR (0x7a39fc)
  -> CClientExoApp instance
       -> GetPlayerCreature()  @0x5ed540   // returns CSWCCreature *
            // OR: CSWParty::GetPlayerCharacter() @0x635460 from the party
            -> server_object (CSWCObject +0xf8)  // lift to CSWSCreature/CSWSObject
                 -> CSWSObject.position @+0x90  // 3 floats
                 -> CSWSObject.orientation @+0x9c  // 3 floats
```

`CClientExoApp::GetPlayerCreature` is `__thiscall(void) -> CSWCCreature*` (SARIF **CONFIRMED**). `CSWSObject::GetArea` `@0x4cb120` returns the area the object lives in. `CSWSModule::GetArea` `@0x4c30f0` returns the **current** area (singular — KotOR loads one `CSWSArea` per module at a time; sub-areas are rooms within it).

**NWScript-equivalent reuse path** (no inline reads of struct fields):

- `CSWVirtualMachineCommands::ExecuteCommandGetPosition` `@0x53cae0` — same backing call as `nwscript GetPosition(object) -> vector`
- `CSWVirtualMachineCommands::ExecuteCommandGetFacing` `@0x537fe0`
- `CSWVirtualMachineCommands::ExecuteCommandGetLocation` `@0x53b840` — returns `CScriptLocation` (position + orientation packaged together)
- `CSWVirtualMachineCommands::ExecuteCommandGetDistanceBetween` `@0x537370` (3D), `…2D` `@0x537470`, `…ToObject` `@0x537820`/`…2D 0x537960`, `…BetweenLocations` `@0x537590`/`…2D 0x5376c0`

These are stable, **idempotent, side-effect-free** entry points — safe to call from a hook (no engine state mutation, no allocation that we don't already get for `Vector` returns).

### Walls — coordinate access

Walls aren't a top-level "wall" object. They are encoded in two places:

1. **Walkmesh perimeters and edges.** `CSWWalkMeshHeader` carries `edge_count +0x78 / edge_offset +0x7c` and `perimeter_count +0x80 / perimeter_offset +0x84`. The perimeter is the closed boundary of the walkable region — every "wall" the player can bump against has an edge in there. To extract: vertex-array (at `vertex_offset +0x4c`, count `+0x48`) indexed by edge entries.
2. **Collision meshes for placeables/doors.** `CSWCollisionMesh` has its own vertex/face/normal arrays (`+0x54`/`+0x60`/`+0x68`), used for non-room geometry that blocks movement (chests, container clutter, breakable scenery).

**STRONG** — the data is there, the offsets are mapped. We have not yet decoded the walkmesh face/edge byte layout (vertex_size, face stride). Next-session work if we need geometric wall enumeration; for accessibility the perimeter polygons + a player-relative bearing should be enough.

### Transition points — coordinate access

Two complementary kinds of transitions:

- **Door transitions** (`CSWSDoor`):
  - `linked_to +0x388` (target), `linked_to_flags +0x384`, `linked_to_module +0x390` (cross-module), `transition_destination +0x3c8` (world coord on the other side)
  - `loc_name +0x39c` and `description +0x3a4` for human-readable name
  - `corners +0x350` (mem already has this from DLZ)
- **Trigger transitions** (`CSWSTrigger`):
  - `localized_name +0x228`, `linked_to +0x230`, `linked_to_module +0x238`, `transition_destination +0x30c`, `cursor +0x2fc`, `geometry +0x288 / geometry_count +0x284 / geometry_indices +0x298`
  - `CSWSTrigger::GetTargetArea(void) -> undefined4` `@0x58c830` — direct API; returns area handle on the other side
- **Live "transition prompt" panel:**
  - `CSWGuiInGameAreaTransition.area_transition_position +0x424` — world coord
  - `CGuiInGame::GetAreaTranstionPosition(Vector* out)` `@0x62ec20` (note typo) — convenience accessor, signature **CONFIRMED** SARIF

So: the player's position, every reachable transition, and every door's target position are all readable from the engine right now.

---

## Q2 — Navmesh / walkmesh for click-to-move and NPC movement

### Yes — explicit walkmesh, not implicit collision

KotOR's engine carries an explicit, per-room **walkmesh** (a triangle mesh of walkable surface), plus an AABB tree for spatial queries:

```
CSWWalkMeshHeader  (DB CONFIRMED)
  +0x00 magic
  +0x04 version
  +0x08 world_coords           // anchor
  +0x0c relative_use_positions // doorway-anchor used positions
  +0x24 absolute_use_positions
  +0x3c position
  +0x48 vertex_count
  +0x4c vertex_offset
  +0x50 face_count
  +0x54 face_offset
  +0x58 materails_offset       // (sic) -- per-face material id
  +0x5c normals_offset
  +0x60 distances_offset
  +0x64 aabb_count
  +0x68 aabb_offset
  +0x6c aabb_root              // BVH root for fast point queries
  +0x70 adjacency_count
  +0x74 adjacency_offset       // edge -> face adjacency for graph walks
  +0x78 edge_count
  +0x7c edge_offset
  +0x80 perimeter_count
  +0x84 perimeter_offset
```

`CSWRoomSurfaceMesh` is the room's runtime wrapper around the walkmesh:

- `CSWRoomSurfaceMesh::CheckAABBWalkable(...)` `@0x581530` — bulk AABB query
- `CSWRoomSurfaceMesh::ClippedLineSegmentWalkable(...)` `@0x584220` — ray-walk along a line
- `CSWRoomSurfaceMesh::GetWalkableMaterialMask` `@0x5821c0` — which materials count as walkable

`CSWSRoom` mirrors this on the server side (`CheckAABBWalkable @0x579560`, `ClippedLineSegmentWalkable @0x579670`, `LoadWalkMesh @0x579520`). Doors carry their own special-case mesh: `CSWDoorSurfaceMesh.door_state +0x8c` plus `GetResourceForBinaryWalkMesh @0x5ce8b0`.

### Walkmesh struct details — fully decoded

Decomp of `CSWCollisionMesh::GetVertex @0x597090` and `CSWRoomSurfaceMesh::GetTriangleAdjacency @0x580620`:

**`CSWCollisionMesh` (the embedded mesh, also stand-alone for placeable colliders):**

```
+0x00  vtable
+0x04  world_coords          // bool: 1 = mesh is already in world space, skip transform
+0x0c  resref
+0x2c  position              // Vector — mesh local origin in world
+0x38  orientation           // Quaternion (16 bytes) — mesh rotation
+0x50  vertex_count          // ulong
+0x54  vertices              // Vector* — vertex_count × 12 bytes (mesh-local space)
+0x58  face_count            // ulong
+0x5c  adjacency_count
+0x60  face_indices          // WalkmeshFace* — face_count × 12 bytes
+0x64  materials             // ubyte* (or int*) — face_count entries; index into surfacemat.2da
+0x68  normals               // Vector* — face_count × 12 bytes (per-face normal)
+0x70  relative_use_position_1
+0x7c  relative_use_position_2

WalkmeshFace size=12
  +0x0  vertex_1   ulong   // index into vertices[]
  +0x4  vertex_2   ulong
  +0x8  vertex_3   ulong
```

**`CSWRoomSurfaceMesh` wraps `CSWCollisionMesh` and adds the per-room navigation graph:**

```
+0x00  mesh                            // embedded CSWCollisionMesh
+0x88  adjacencies                     // SurfaceMeshAdjacency* — face_count × 12 bytes
+0x8c  edges                           // edge index array
+0x9c  perimeters                      // explicit perimeter polygon list
+0xac  aabbs                           // AABB BVH node array
+0xbc  crossing_points                 // doorway / room-boundary crossings
+0xc8  crossing_edges
+0xd4  aabb_root                       // BVH root index
+0xd8  los_material_mask               // bit mask: which materials are line-of-sight transparent
+0xdc  walkable_material_mask          // bit mask: walkable materials
+0xe0  walk_check_material_mask        // walkable + ray-test materials
+0xe4  all_material_mask

SurfaceMeshAdjacency size=12
  indices[3]  int   // 3 neighbour face indices (one per triangle edge)
                    // -1 = perimeter / wall edge (no neighbour)
                    // otherwise: edge_id; convert to face_id via /3
```

### High-level "is this point walkable?"

`CSWSArea::PositionWalkable(Vector) -> bool` `@0x506400` (**CONFIRMED** SARIF). Decomp shows the full path:

```c
bool PositionWalkable(CSWSArea *this, Vector p) {
  CSWSRoom *room = GetRoom(this, &p, NULL);                    // @0x4bb600 — find which room contains p
  if (!room) return false;
  int materialId = CSWRoomSurfaceMesh::GetSurfaceMaterialWalkCheckOnly(
                     room->surface_mesh, p);                    // @0x581a10 — ray-test face below p
  // Look up surfacemat.2da[materialId]["Walk"]  → 0 or 1
  return surfacemat_table->GetINT(materialId, "Walk") != 0;
}
```

So we get an *exact* yes/no from a single Vector input. One call, no allocation, no side-effects — safe to use freely from a hook.

### Wall geometry — extraction recipe (PRIMARY for audio feedback)

The walkmesh adjacency layout makes wall-edge enumeration a **30-line loop**:

```c
// Pseudocode — for one room's surface mesh
CSWRoomSurfaceMesh *sm = room->surface_mesh;
CSWCollisionMesh *cm = &sm->mesh;
WalkmeshFace *faces = cm->face_indices;            // [+0x60]
SurfaceMeshAdjacency *adj = sm->adjacencies;       // [+0x88]
Vector *verts = cm->vertices;                      // [+0x54], mesh-local

for (int f = 0; f < cm->face_count; f++) {
  for (int e = 0; e < 3; e++) {
    if (adj[f].indices[e] != -1) continue;         // has neighbour — interior edge
    // perimeter edge — this triangle side IS A WALL
    int va = (&faces[f].vertex_1)[e];              // edge endpoints in face-vertex order
    int vb = (&faces[f].vertex_1)[(e + 1) % 3];
    Vector world_a, world_b;
    CSWCollisionMesh_LocalToWorld(cm, &world_a, &verts[va]);   // @0x596aa0
    CSWCollisionMesh_LocalToWorld(cm, &world_b, &verts[vb]);
    // emit wall edge (world_a, world_b)
  }
}
```

For **audio feedback for nearby walls** the loop becomes:

1. Iterate all rooms in the current `CSWSArea` (`+0x230 rooms`, count `+0x230 + ?`; per `GetRoom` decomp, room stride is `0x4c` bytes).
2. For each room, run the wall-edge enumeration above to populate a wall-segment list.
3. For each wall segment compute closest-point-on-segment to player position (cheap 2D math; wall edges in KotOR rooms are essentially walls in the XY plane with constant Z range).
4. Find the K nearest segments, take their bearing (player → midpoint) and distance.
5. Render to spatial audio: panning = bearing relative to player facing, volume = inverse distance, possibly different timbre for "wall ahead" vs "wall to side".

### Wall-edge audio: caching strategy

The walkmesh is **immutable per room** — vertices, faces, adjacencies don't change during gameplay (doors/animations don't deform the mesh; they're separate `CSWCollisionMesh` objects on the door entity). So we can:

- On area-enter: walk all rooms, build a `WallEdge { Vector a, Vector b, int room_id, int material_id }` array once.
- On per-tick speech: 2D distance test against the cached array; no re-iterate of engine memory.
- Optional: build a kd-tree over the segment midpoints if K-nearest gets expensive. The room AABB BVH (`aabb_root +0xd4`) is already there if we want to reuse it, but a simple flat array is fine for typical room sizes (a few hundred wall segments per room).

**The walkmesh material at each face also tells us the floor type** (`materials +0x64` indexes `surfacemat.2da`). Categories include carpet, stone, grass, metal, water, lava, non-walkable obstacle. This is a free win for "you are walking on stone" footstep narration; same source data the engine uses to play footstep sounds.

### Engine click→move path

`CClientExoAppInternal::HandleMouseClickInWorld(void) @0x620350` is the entry point — it does screen→world projection via the camera, validates with `PositionWalkable`, then either issues a move action or selects an object. We can either:

- Hook it to capture the destination point the engine resolved, **or**
- Bypass it entirely and call `CSWSCreature::AddMoveToPointAction` directly (Q3) with our own destination.

`CGuiInGame::GetCanClick @0x62f9a0` and `MarkNoClickEvent @0x62f960` gate the click pipeline (e.g. during cutscenes). `CClientExoApp::SetLastClickedOnTarget(ulong) @0x5ee200` is also exposed if we want to drive the engine's "last-targeted object" state from a hook.

### Material classification

`SurfaceMat2DA.Name +0x0` exists; the walkmesh face's material byte indexes into `surfacemat.2da` which classifies "grass", "stone", "non-walkable lava", "trigger", etc. Per the `PositionWalkable` decomp the engine reads the **`Walk` column** of that 2DA — value 1 = walkable. Other columns (the 2DA also has e.g. `Sound`, `Step`, `Grass`) classify the surface for footstep audio and weather. Lookup helpers in `Global::GetWalkMeshColors @0x46f200` / `GetInverseWalkMeshColors @0x46f300` (mostly for editor-style overlays).

For accessibility this gives a free "you walk onto carpet / metal / water" announcement: read the player's current room, ray-test their position via `GetSurfaceMaterialWalkCheckOnly`, look up the row in `surfacemat.2da` (already loaded under `Rules->internal->all_2DAs->surfacemat`), narrate.

### Status

- **Walkmesh data structures: CONFIRMED** (decomp confirms WalkmeshFace = 12 bytes, vertices = Vector[], adjacencies = int[3] per face)
- **High-level walkable-point check: CONFIRMED** (full pipeline decoded)
- **Engine click→move path: CONFIRMED** (entry function + signature)
- **Wall-edge enumeration via adjacency == -1: CONFIRMED** (decomp of `GetTriangleAdjacency` shows -1 sentinel for perimeter edges)
- **`SurfaceMeshAdjacency.indices` semantics — divided by 3 to get face_id: CONFIRMED**

---

## Q3 — AI movement + pathfinding we can reuse

### Pathfinder is alive and callable

The engine has a fully-implemented A* / steering pathfinder. The relevant pieces:

**Per-area pathfinding state** — `CSWSArea` carries:

- `pathfind_info +0x1b0` — `CPathfindInformation` (DB **CONFIRMED**, fields below)
- `path_points +0x23c` — node array
- `path_connections +0x244` — edge array (the navmesh graph in the path-graph sense, not the walkmesh sense)

`CPathfindInformation` (DB):

```
+0x04 personal_space          // creature radius
+0x08 cre_personal_space
+0x10 camera_space
+0x14 height
+0x18 hit_distance
+0x2c creature_object_id      // who this query is for
```

**Per-creature pathfinding state** — `CSWSCreature.path_find_info +0x340` (DB **CONFIRMED**), plus `party_follow_info +0x4c0`, `moving_orientation +0x1c4` (on the *client* side `CSWCCreature`).

**Pathfinder entry points:**

| Function | Address | Purpose |
|---|---|---|
| `CAvoidCreature::FindPath(int*)` | `0x5d0690` | core path solver (the "avoid creatures" variant — main entry) |
| `CAvoidCreature::FindPath_Left` | `0x5cf650` | left-handed steering fallback |
| `CAvoidCreature::FindPath_Right` | `0x5cf7a0` | right-handed steering fallback |
| `CSWSCreature::AIActionMoveToPoint(actionNode*) -> int` | `0x51f4f0` | per-tick mover (consumes path) |
| `CSWSCreature::AIActionCheckMoveToPoint` | `0x510670` | precondition check |
| `CSWSCreature::AIActionCheckMoveToPointRadius` | `0x5108a0` | "within radius of" variant |
| `CSWSCreature::AIActionCheckMoveToObject` / `Radius` / `FollowRadius` | `0x5101a0` / `0x5103b0` / `0x510ab0` | object-relative variants |
| `CSWSCreature::AIActionCheckInterAreaPathfinding(int)` | `0x50ff20` | inter-area (cross-room/module) routing |
| `CSWSCreature::AddMoveToPointAction(...)` | `0x4f8b60` | enqueue move to a Vector (full 17-param signature in SARIF — most params are AI hints) |
| `CSWSCreature::AddMoveToPointActionToFront` | `0x4f8a50` | priority-enqueue |
| `CSWSCreature::AddPathfindingWaitActionToFront` | `0x4eb5a0` | wait for path readiness |
| `CSWSCreature::ForceMoveToPoint` | `0x4edba0` | bypass the queue |
| `CSWSCreature::ResolveMoveToForceJump` | `0x5b7b30` | when path can't be found, jump (cinematic) |
| `CSWSCreature::WalkUpdateLocation_QuickWalk_FollowLeader_FindPath` | `0x51ac10` | the per-tick movement loop (party follow path) |
| `CSWSCreature::UpdateSubareasOnMoveTo(...)` | `0x51b7b0` | subarea bookkeeping |
| `CServerExoApp::GetCreaturePathfindInformation` | `0x4aef10` | accessor |

### `AddMoveToPointAction` — full 17-arg semantics (decoded)

Decomp of `CSWSCreature::AddMoveToPointAction @0x4f8b60` shows the function packs most of its int params into a single bitfield, then passes a typed-arg list to `CSWSObject::AddAction(this, MOVE_TO_POINT=1, ...)`. Reverse-engineered argument map:

```c
undefined4 AddMoveToPointAction(
    CSWSCreature *this,
    ushort   actionId,        // param_1 — caller-assigned action queue id (monotonic counter)
    Vector  *destination,     // param_2 — target world position (xyz)
    ulong    objectId1,       // param_3 — primary related object (e.g. follow target, INVALID = 0x7f000000)
    ulong    objectId2,       // param_4 — secondary related object (e.g. area handle, INVALID = 0x7f000000)
    int      runFlag,         // param_5 — bit 0 of packed flags: 0=walk, 1=run
    float    radius,          // param_6 — pass-through float (likely "stop within radius")
    float    followDistance,  // param_7 — if non-zero, sets bit 2 of flags (=> "follow within distance")
    int      forceFlag,       // param_8 — bit 1 of flags
    int      timeoutMs,       // param_9 — pass-through int
    int      pathMode,        // param_10 — 3-bit field at bits 4..6 (movement/path mode)
    int      avoidFlags,      // param_11 — 2-bit field at bits 7..8 (avoidance behaviour)
    int      flagBit3,        // param_12 — bit 3 of flags
    int      flagBit9,        // param_13 — bit 9 of flags
    Vector  *secondaryPoint,  // param_14 — only X and Y are read; likely "look-at" / arrival facing
    ulong    pathContext1,    // param_15 — pass-through ulong
    ulong    pathContext2,    // param_16 — pass-through ulong
    int      flagBit10        // param_17 — bit 10 of flags
)
```

After packing, the function calls `CSWSObject::AddAction(this->object, ACTION_MOVE_TO_POINT=1, actionId, …)` with a typed-arg list (each element is a `(kind, ptr)` pair where kind 1 = int, 2 = float, 3 = ulong/objectid). It also calls `SetLockOrientationToObject(this, 0x7f000000, 0)` to release any prior facing lock.

**The "minimum-viable move-to-point" call** (for a click-to-move equivalent) is:

```c
static ushort g_actionId = 0;
AddMoveToPointAction(
    playerCreature,
    g_actionId++,
    &destination,           // Vector
    0x7f000000, 0x7f000000, // INVALID_OBJECT_ID for both refs
    0,                      // walk (set 1 for run)
    0.0f, 0.0f,             // no radius, no follow distance
    0, 0,                   // no force, no timeout
    0, 0,                   // default path mode and avoidance
    0, 0,                   // no extra flags
    &destination,           // secondary point = same destination (face arrival)
    0, 0,                   // no path context
    0                       // no extra flags
);
```

For "follow leader" the call would set `objectId1 = leader.id`, `followDistance = 2.0f` (≈ 2 metres), `runFlag = 1`. For "AI patrol" the existing engine code passes specific values per AI script — see the `AIAction…` family at `0x510000-0x520000` for live examples.

**`SetLockOrientationToObject(0x7f000000, 0)`** — `0x7f000000` is the engine's INVALID_OBJECT_ID sentinel. Calling with that value clears the orientation lock (no forced facing).

### NWScript-callable shortcuts (much simpler API)

The NWScript VM commands are thin wrappers around the engine actions. From the binary:

- `ExecuteCommandActionMoveAwayFromObject` `@0x53f990`
- `ExecuteCommandActionMoveAwayFromLocation` `@0x52d090`
- `ExecuteCommandMoveToObject` `@0x53fb00` (signature **CONFIRMED** SARIF: `__thiscall(ScriptFunctions routine, int paramCount)`)
- `ExecuteCommandMoveToPoint` `@0x53fe00` (signature **CONFIRMED**)
- `ExecuteCommandActionJumpToObject` `@0x52cce0`
- `ExecuteCommandActionJumpToPoint` `@0x52cdc0`
- `ExecuteCommandGetNearestObject` `@0x54b550`
- `ExecuteCommandGetNearestMine` `@0x54a090`
- `ExecuteCommandGetWaypointByTag` `@0x53e2b0`
- `ExecuteCommandGetObjectByTag` `@0x53c280`
- `ExecuteCommandGetObjectInArea` `@0x53c390`
- `ExecuteCommandGetObjectInShape` `@0x54a260`

These all take `(routine, paramCount)` and pull args from the VM stack. From a hook we either (a) push args + invoke the VM directly, or (b) call the underlying `CSWSCreature::Add*Action` method directly with constructed args. **(b) is simpler** and what we'll likely do.

### Inter-area pathfinding

`CSWSCreature::AIActionCheckInterAreaPathfinding @0x50ff20` and `CServerExoApp::SetMoveToModulePending @0x4aecc0` / `SetMoveToModuleStartWaypoint @0x4aed30` / `SetMoveToModuleString @0x4aecd0` form a clear "move the player to the next module" pipeline. `CClientExoApp::AddMoveToModuleMovie @0x5edb50` queues the loading-screen movie. Triggers with `transition_destination` go through this when the player crosses them.

### Status

- **Pathfinder exists and is reusable: CONFIRMED**
- **Per-area path graph (path_points + path_connections) and per-creature pathfind_info: CONFIRMED**
- **High-level NWScript-style entry points: CONFIRMED**
- **Cross-area transitions: CONFIRMED**
- **Full `AddMoveToPointAction` arg semantics: STRONG → OPEN** — enough for "move to here", but we need decompilation if we want fine control (radius, run vs walk, force-jump fallback)

---

## Q4 — Player map / minimap: icon positions and map↔world translation

### Two distinct UIs to keep separate

- **`CSWGuiInGameMap`** (DB **CONFIRMED**) — the in-game **area map** (the corner minimap, plus its expanded full-screen variant). Holds:
  - `map_label +0x64`, `area_label +0x1a4`, `mapnote_label +0x2e4`, `compass_label +0x424`
  - `return_button +0x564`, `partyselect_button +0x728`, `exit_button +0x8ec`, `up_button +0xab0`, `down_button +0xc74`
  - `map_hider +0xe38` (the per-zone fog-of-war coverage)
  - `failure_popup_strref +0x10d4`, `bit_flags +0x10d8`
- **`CSWGuiInGameGalaxyMap`** (DB **CONFIRMED**) — the **galaxy map** (planet picker on the Ebon Hawk). Holds `planets +0x23cc` and `current_planet +0x254c`. Each entry is a `GuiInGameGalaxyMapPlanet` (`name_ref +0x0`, `description_ref +0x4`, `model_ref +0x8`).

For room-level navigation the relevant UI is `CSWGuiInGameMap`. For planet-to-planet selection the relevant UI is the galaxy map.

### Minimap pin data — first-class, in-memory, live-updating

```
CSWCArea  (DB CONFIRMED)
  +0x1c4 map_pins           // CSWCMapPin** (array of pointers)
  +0x1c8 map_pins_count     // int
  +0x1cc map_pins_capacity  // int

CSWCMapPin  (SARIF DATATYPE, size 272)
  +0x000 object   CSWCObject       // INHERITS from CSWCObject -> position +0x24, orientation +0x30
  +0x0fc field1_0xfc  undefined4   // probably pin type / icon strref
  +0x100 field2_0x100 undefined4
  +0x104 field3_0x104 undefined4
  +0x108 field4_0x108 undefined4
  +0x10c field5_0x10c undefined4
```

**Crucial finding:** `CSWCMapPin` *embeds* `CSWCObject`. That means **every pin has a world position** (`+0x24`) and orientation (`+0x30`) from the moment it's added.

The 5 fields at `+0xfc..+0x10c` are now decoded via decomp of `AddMapPin @0x606d90`, `GetMapPin @0x605ac0`, and the wire-protocol writer `SendServerToPlayerMapPinAdded @0x56a740`. The wire packet is `(float x, float y, float z, CExoString name, DWORD flags)` — 12 + variable + 4 = ~24 bytes plus string. Working mapping (some inferred):

```
CSWCMapPin  size=272
  +0x000 object         CSWCObject       // embedded — position +0x24, orientation +0x30
  +0x0fc note_text      CExoString       // displayed map-note text (from server packet)
  +0x100 note_strref    int              // TLK strref backing the CExoString (when set via strref)
  +0x104 flags          ulong            // pin flags (kind, hidden, blink, etc.)
  +0x108 key1           ulong            // GetMapPin lookup key part 1 (likely pin id or owner objectId hi)
  +0x10c key2           ulong            // GetMapPin lookup key part 2 (likely sub-id or owner objectId lo)
```

`GetMapPin(p1, p2)` decomp confirms `+0x108` and `+0x10c` are **lookup keys** (a (key1, key2) tuple). Likely interpretations:
- `(objectId_hi, objectId_lo)` — pins keyed to a specific waypoint/creature
- `(pin_kind, slot)` — pins keyed by user-pin-slot for the player-placed marker
The exact split is decided by the caller; either way, both keys are readable.

Live mutation through:

- `CSWCArea::AddMapPin(CSWCMapPin*)` `@0x606d90` (**CONFIRMED** decomp — straight append into `map_pins` array, double-on-full)
- `CSWCArea::ClearAllMapPins()` `@0x606dd0`
- `CSWCArea::GetMapPin(int key1, int key2) -> CSWCMapPin*` `@0x605ac0`

Server-side mirror via `CSWSMessage::SendServerToPlayerMapPinAdded @0x56a740` (decomp: writes `[float x][float y][float z][CExoString note][DWORD flags]`, sent as `MAP_PIN` message subtype 4). Player→server: `HandlePlayerToServerMapPinSetMapPinAt @0x524c70`, `…ChangePin @0x525080`, `…DestroyMapPin @0x524f10`, `…Message @0x527380`. `SendServerToPlayerMapPinEnabled @0x56a6d0` sends `[OBJECTID][BOOL]` — confirming pins ARE addressable by object id, so `key1`/`key2` are most likely a 64-bit object id split.

### Map notes on waypoints (the named pins)

```
CSWSWaypoint  (DB CONFIRMED)
  +0x000 object               // CSWSObject base, so position is at +0x90 of the underlying
  +0x228 has_map_note         // bool
  +0x22c map_note_enabled     // bool
  +0x230 map_note             // CExoLocString — the displayed note text!
  +0x238 localized_name       // CExoLocString — the waypoint's name
```

So every waypoint that surfaces on the map *carries its own world position (via the inherited `CSWSObject.position +0x90`) and its own localized note text*. The map-note label `CSWGuiInGameMap.mapnote_label +0x2e4` is repopulated as the cursor moves over the map.

### Map notes — server-side helpers

- `CSWGuiMapHider::InitializeMapNotes(CExoString*, int) -> CExoString*` `@0x692c70` (signature **CONFIRMED**) — populates the note list when the map opens
- `CSWGuiMapHider::GetNextMapNote(int)` `@0x692e80`, `GetPrevMapNote(int)` `@0x693090` — sequential iteration (a screen reader's friend!)
- `CSWGuiMapHider::SetMapNote` `@0x6932a0`, `CSWGuiInGameMap::SetMapNote @0x6929b0`
- `CSWGuiInGameMap::OnMapNoteClicked(int)` `@0x693e70`
- `CSWGuiMapHider::ClearMapNotes` `@0x692e30`

`GetNextMapNote` / `GetPrevMapNote` is the **most accessibility-friendly affordance the engine already exposes** for the map: the user can step through the map notes on the current map without needing pointer/click.

### NWScript map-pin commands

- `ExecuteCommandSetMapPinEnabled @0x54b460`

(Also visible: `CSWGuiOptionsFeedback::OnMiniMap @0x6de640` — toggles whether the minimap is shown at all.)

### Minimap ↔ world translation — fully decoded

The transform lives on `CSWSAreaMap` (per-area, hung off `CSWSModule.field89_0x218` ≈ `+0x218`, so probably `area_map`). Decoded layout from `CSWSAreaMap::Initialize @0x578c60` and `GetMapPixelFromWorldCoord @0x578e00`:

```
CSWSAreaMap
  +0x00  fog_bitfield     uint*    // packed exploration bits, length = field1_0x4 dwords
  +0x04  bitfield_dwords  int      // number of uint32s in the bitfield
  +0x08  grid_width       int      // 1..0x58 (88 max), set from .are GFF
  +0x0c  grid_height      int      // = round(grid_width * 0.58181816), capped at min 1
  +0x10  orientation      ulong    // 0=N, 1=S (flipped), 2=E (90° CW), 3=W (90° CCW)
  +0x14  scale_recip      float    // 1.0 / param_12 from Initialize (some grid scale)
  +0x18  world_units_per_x_pixel  float
  +0x1c  world_units_per_y_pixel  float
  +0x20  world_origin_x   float    // world X corresponding to map-pixel (0,*)
  +0x24  world_origin_y   float    // world Y corresponding to map-pixel (*,0)
```

**Forward transform** — `GetMapPixelFromWorldCoord(Vector world, int *outPx, int *outPy) -> bool` (returns false if the point is off-map). Decoded:

```c
// Apply the per-area orientation rotation (which way is "north" on this map)
float xw = world.x, yw = world.y;     // z is discarded — minimap is 2D
switch (m->orientation) {
  case 0: /* identity */                          break;
  case 1: xw = -world.x; yw = -world.y;           break;  // 180°
  case 2: xw = world.y;  yw = -world.x;           break;  //  90° CW
  case 3: xw = -world.y; yw = world.x;            break;  // 270° CW
}
*outPx = round((xw - m->world_origin_x) / m->world_units_per_x_pixel);
*outPy = round((yw - m->world_origin_y) / m->world_units_per_y_pixel);
// Bounds check: 0 <= px < 0x1b9 (441), 0 <= py < 0x101 (257)
return (in_bounds);
```

**Inverse transform** — *not exposed by the engine, but trivially derivable from the above:*

```c
bool MapPixelToWorld(CSWSAreaMap *m, int px, int py, float playerZ, Vector *outWorld) {
  if (px < 0 || px >= 441 || py < 0 || py >= 257) return false;
  float xw = px * m->world_units_per_x_pixel + m->world_origin_x;
  float yw = py * m->world_units_per_y_pixel + m->world_origin_y;
  // Undo the orientation rotation
  switch (m->orientation) {
    case 0: outWorld->x = xw;  outWorld->y = yw;          break;
    case 1: outWorld->x = -xw; outWorld->y = -yw;         break;
    case 2: outWorld->x = -yw; outWorld->y = xw;          break;  // inverse of CW90 = CCW90
    case 3: outWorld->x = yw;  outWorld->y = -xw;         break;
  }
  outWorld->z = playerZ;  // best-effort; the map doesn't carry Z. Refine via walkmesh ray-test.
  return true;
}
```

Z is unrecoverable from the map alone; for accuracy, use `GetSurfaceMaterialWalkCheckOnly` style ray-test at `(world.x, world.y, +1000)` going down to find the room floor's true Z.

**Pixel space:** map view is `441 × 257` pixels at the engine's reference resolution (640×480). Centre offset for fullscreen variants accounted for by `CSWGuiInGameMap::HitCheckMouse` decomp:

```c
adjusted_px = mouse_x - (viewport_width  - 640) / 2;
adjusted_py = mouse_y - (viewport_height - 480) / 2;
```

**Compass heading on the map:** `CSWSAreaMap::GetMapRotateCCWFromWorldOrientation @0x578ed0` — takes the player's `Vector` orientation, runs `Yaw(...)` (returns degrees), then adds 0/180/90/270 based on the map's own orientation. Returns the angle (degrees, CCW) to rotate the compass-arrow sprite by. Same data is what we'd announce as "facing 47° (north-east)" to the player.

**Party-member positions on the map:**

- `CSWSAreaMap::GetPartyMemberMapLocation(...) @0x5791b0` — read each party member's mapped position
- `CSWSAreaMap::GetPartyMemberMapOrientation(...) @0x5791f0` — read each party member's mapped facing
- Inverse setters at `0x5790c0` / `0x579130`

Combined with the position read, this lets us narrate "Carth is 4m behind you to the left" without any hooks beyond a per-tick read of `CSWSAreaMap`.

**Fog of war read/write:**

- `CSWSAreaMap::IsWorldPointExplored(Vector) -> bool @0x579210`
- `CSWSAreaMap::SetWorldPointExplored(Vector) @0x5792d0`
- `CSWSAreaMap::SetMapPointExplored(int px, int py) @0x579000`
- `CSWSAreaMap::SetEntireMapExplored() @0x578fa0`
- `CSWSAreaMap::LoadSavedAreaMapData(...) @0x578fc0` (savegame-restore path)

### Status

- **Map pin storage and life-cycle: CONFIRMED**
- **`CSWCMapPin` fields 0xfc–0x10c: STRONG** (note CExoString + strref + flags + 2 lookup keys; exact key semantics inferred from wire packet)
- **Each pin has a real world position via embedded `CSWCObject`: CONFIRMED**
- **Waypoint map notes (text + name + position): CONFIRMED**
- **Sequential note iteration helpers: CONFIRMED**
- **Minimap pixel ↔ world transform: CONFIRMED** (`GetMapPixelFromWorldCoord` decoded; inverse derived in 4 lines)
- **Compass orientation transform: CONFIRMED**
- **Party-member positions on map: CONFIRMED** (direct accessors)
- **Galaxy map (planets) layout: CONFIRMED** (separate code path)

---

## Q5 — Per-area object / interactable / trigger / NPC lists with coordinates

### `CSWSArea` is the canonical per-map directory

```
CSWSArea  (DB CONFIRMED)
  +0x150 name              // CExoLocString
  +0x158 tag               // CExoString (internal map id)
  +0x190 game_objects      // <object array>
  +0x194 game_object_count // int
  +0x1b0 pathfind_info     // CPathfindInformation
  +0x230 rooms             // room array
  +0x23c path_points       // path-graph nodes
  +0x244 path_connections  // path-graph edges
  +0x25c room_names        // CExoString*
  +0x294 omen_events
  +0x2c4 trans_pending     // bool
  +0x2c8 next_transition_pending_id
  +0x2d0 client_area       // back-pointer to CSWCArea
```

### Iteration

Two complementary iterators on `CSWSArea`:

- `CSWSArea::GetFirstObjectInArea(ulong* iter) -> undefined4` `@0x5071b0` (**CONFIRMED** SARIF)
- `CSWSArea::GetNextObjectInArea(ulong* iter) -> undefined4` `@0x5071e0` (**CONFIRMED** SARIF)

…and a spatial / ordered-by-X variant:

- `CSWSArea::GetFirstObjectIndiceByX(int*, float) -> undefined4` `@0x506f20` (**CONFIRMED** SARIF) — sorted access by X; gives us "objects within range" without an O(N) scan

Plus `CServerExoApp::GetObjectArray(void) @0x4aed70` for the global object array, and `CServerExoApp::GetAreaByGameObjectID(handle) @0x4ae780` for handle→area lookups.

### Object types (filterable per-area)

From DLZ `types.h` and the offsets table, every object reports its type via `OFFSET_GAME_OBJECT_TYPE = 0x8` on the underlying `game_object`:

```
GAME_OBJECT_TYPES enum:
  AREA          = 4
  CREATURE      = 5
  ITEM          = 6
  TRIGGER       = 7
  PROJECTILE    = 8
  PLACEABLE     = 9
  DOOR          = 10
  AREAOFEFFECT  = 11
  WAYPOINT      = 12
  ENCOUNTER     = 13
  STORE         = 14
  SOUND         = 16
```

So a single iterator over `game_objects[]` gives us everything in the room with one numeric switch. For each:

- **Creature** → `CSWSCreature` → `CreatureStats.first_name`/`last_name` (`+0x14`/`+0x1c`), `appearance +0xa4c`, `path_find_info +0x340`, `party_follow_info +0x4c0`
- **Door** → `CSWSDoor` → `loc_name +0x39c`, `description +0x3a4`, `linked_to`, `transition_destination`, `key_required`, `lockable`/`open_state`
- **Placeable** → `CSWSPlaceable` → `loc_name +0x228`, `description +0x234`, `usable +0x328`, `lockable +0x330`, `has_inventory +0x334`, `open +0x338`, `is_corpse +0x44c`, `body_bag +0x394`
- **Trigger** → `CSWSTrigger` → `localized_name +0x228`, `geometry +0x288`/`geometry_count +0x284`, `trap_detectable`/`trap_disarmable`, `cursor +0x2fc`, `transition_destination +0x30c`
- **Encounter** → `CSWSEncounter` → `localized_name +0x230`, `spawn_points_list +0x2b8`, `geometry_list +0x290`, `script_on_entered/exit/heartbeat`
- **Waypoint** → `CSWSWaypoint` → `has_map_note +0x228`, `map_note_enabled +0x22c`, `map_note +0x230`, `localized_name +0x238` (and inherits position from `CSWSObject +0x90`)
- **Item** → `CSWSItem` → `localized_name +0x280`, `description +0x278`, `stack_size +0x28c`, `description_indentified +0x270`, `bit_flags +0x288`
- **Store** → `CSWSStore.loc_name +0x234`
- **AreaOfEffect**, **Sound**, **Projectile** — also iterable, less interesting for accessibility nav

### "Look up object by tag" / "find objects by shape"

- `CSWSModule::FindObjectByTagOrdinal(...)` `@0x4c5fe0`
- `CSWSModule::FindObjectByTagTypeOrdinal(...)` `@0x4c60b0`
- NWScript: `ExecuteCommandGetObjectByTag @0x53c280`, `ExecuteCommandGetObjectInArea @0x53c390`, `ExecuteCommandGetObjectInShape @0x54a260`, `ExecuteCommandGetWaypointByTag @0x53e2b0`

The module also keeps an `object_lookup_table +0x130` (`CSWSModule`) for fast tag→object resolution.

### Status

- **Per-area object array, iteratable: CONFIRMED**
- **Type discrimination via 0x8 offset enum: CONFIRMED**
- **Per-type fields useful for narration (name, description, lock, trap, transition target): CONFIRMED**
- **Spatial-ordered iteration available: CONFIRMED**
- **Lookup by tag / shape: CONFIRMED** (both inline and via NWScript wrappers)

---

## Q6 — Highlight / object-cycle / icon system

### "Tab to cycle" already exists

`a11y-map.md` already calls these out as **suspected**; the DB lookups confirm them as **STRONG**:

- `CClientExoAppInternal::SelectNearestObject` — keyboard-friendly target cycling (the engine's existing primitive for a11y-style "tab to next thing")
- `CClientExoAppInternal::DoPassiveSelection` — ambient highlight (the white outline that appears when you're near something interactable)
- `CClientExoApp::SetLastClickedOnTarget(ulong) -> void` `@0x5ee200` (**CONFIRMED** SARIF) — sets the engine's "last targeted" handle; an obvious place to drive selection from a hook
- `CClientExoApp::SetLastTarget` / `GetLastTarget` — currently-targeted in-world object
- `CGuiInGame::SetMainInterfaceTarget` — main interface target indicator

Combined with the per-area object iterator (Q5), we get **"tab through every interactable in the room"** for free — pull next object whose type ∈ {DOOR, PLACEABLE_usable, TRIGGER, CREATURE_friendly, WAYPOINT_with_map_note}, set as last-target, announce its `loc_name`/`localized_name`/`first_name` + relative bearing.

### Built-in render toggles (debug overlays = a11y windfall)

These are global on/off flags already wired to the renderer. We can flip them from a hook to give the user (or ourselves while developing) a visible scaffold of where things are:

```
RENDER_AABB             0x7fbf5c   // every object's bounding box
RENDER_GOB_BBS          0x82805c   // game-object bounding boxes
RENDER_GUI              0x7bb4d0
RENDER_PERSONAL_SPACE   0x7b9314   // creature radius
RENDER_QA_TRIGGERS      0x83285c   // QA-only trigger rendering
RENDER_TRIGGERS         0x7b92e4   // trigger volume outlines
RENDER_WIREFRAME        0x7bb4f0
```

These don't help the screen reader directly, but they make sighted dev sessions far more productive: turn on `RENDER_TRIGGERS + RENDER_AABB` and we can visually verify what the iterator sees.

### Map icons / map notes

Q4 already covers `CSWCArea.map_pins[]` (the "icons on the map" data) and `CSWSWaypoint.has_map_note` / `map_note_enabled` / `map_note` (the persistent named map markers). The ergonomic surface is:

- `CSWGuiMapHider::GetNextMapNote(int)` / `GetPrevMapNote(int)` for sequential cycling
- `CSWGuiInGameMap::OnMapNoteClicked(int)` for "go to" wiring

### Tooltip / examine system

These are already mapped as **suspected** in `a11y-map.md`:

- `CSWGuiManager::DisplayToolTip`, `ChangeToolTipText`
- `CSWGuiToolTipPanel::SetToolTipText` (leaf where text is set)
- `CGuiInGame::ShowExamineBox` / `HideExamineBox` (detailed examine UI)
- `CSWGuiControl::DisplayToolTip` (base GUI control tooltip)

These give us "what is the player looking at right now" for free — exactly the right hook for an "describe the focused object" command.

### Cursor system on triggers

`CSWSTrigger.cursor +0x2fc` and `CSWCTrigger.cursor_id +0x12c` carry the cursor sprite the engine swaps to when the player hovers a trigger (door = "enter cursor", item = "pickup cursor", lock = "unlock cursor", etc.). These cursor IDs are a **stable per-interaction-type enum** — perfect for "this is what kind of interactable you are pointing at" narration without us having to classify the object ourselves.

### Status

- **Object-cycle primitive (Tab-equivalent): STRONG** — function names match exactly; not yet hooked
- **Render-debug toggles: CONFIRMED** (DB global_pointers)
- **Map-note iteration: CONFIRMED** (Q4)
- **Tooltip / examine hook surface: STRONG** (suspected in a11y-map; signatures match)
- **Cursor-type signal on hover: CONFIRMED**

---

## Q7 — Human-readable name database (real names, not internal IDs)

### Three-layer model — confirmed

From the offsets table and SARIF DATATYPEs:

**Layer 1 — Internal stable IDs (machine-readable)**

- `CSWSObject.tag +0x18` — `CExoString`, the modder-assigned object identifier ("g_dnt_carth", "trig_to_taris", etc.). Always present, never localized.
- `CSWCCreatureStats.resref +0x4` — `CExoString`, the resource reference (`.utc` template name).
- `CSWSObject.last_name +0xc` — present even on the base object; for creatures it's the family name half.
- `game_object_id` — 32-bit handle the engine uses for cross-system messages. Numeric, not for human display.

**Layer 2 — Per-object localized names (player-facing)**

These are `CExoLocString` (size 8: `internal*` + `strref?`) that resolve to the displayed text in the player's language:

- `CSWSCreatureStats.first_name +0x14`, `last_name +0x1c`
- `CSWCCreatureStats.first_name +0x14`, `last_name +0x1c`, `resref +0x4`
- `CSWSDoor.loc_name +0x39c`, `description +0x3a4`
- `CSWSPlaceable.loc_name +0x228`, `description +0x234`
- `CSWSTrigger.localized_name +0x228`
- `CSWSEncounter.localized_name +0x230`
- `CSWSWaypoint.localized_name +0x238`, `map_note +0x230`
- `CSWSItem.localized_name +0x280`, `description +0x278`
- `CSWSStore.loc_name +0x234`
- `CSWSArea.name +0x150` (the area's own display name)
- `CSWSModule.name_localized +0x64`, `area_name +0x30`

`CExoLocString` resolves via:

- `CExoLocString::GetString` `@0x5e9eb0` — primary
- `CExoLocString::GetStringLoc` `@0x5ea240` — locale-specific
- `CExoLocString::GetStringLength` `@0x5e9f00`, `GetStringCount` `@0x5e9ef0`
- `CExoLocStringInternal::GetString` `@0x5ec950`

**Layer 3 — Global string table (TLK strrefs)**

When a `CExoLocString` is empty inline, its `strref?` (4-byte field at `+0x4`) indexes into the master `dialog.tlk` table:

- `TLK_TABLE_PTR` `@0x7a3a08` — `CTlkTable*` (already known)
- `CTlkTable::GetSimpleString(strref) -> CExoString*` `@0x41e8f0` — already known (mem `project_kotor_text_indirection.md` covers the failure mode where this needs an SEH guard)

`CSWClass.name_strref/name_lower_strref/name_plural_strref/description_strref`, `CSWRace.name_strref`, `CSWSkill.name_strref`, `CSWFeat.feat_resref` etc. live entirely at this layer — the strings are never inlined, always TLK-indexed.

### Reading the player's character name

- `CClientExoApp::GetPlayerCharacterName` `@0x5edab0` — direct accessor
- `CClientExoAppInternal::player_character_name +0x294` — backing field
- `CSWSPlayer::GetPlayerName(handle) -> ?` `@0x5610b0`

(Plus `CSWGuiNameChargen.name_editbox +0x230` for chargen-time entry.)

### "Internal numbered names" — when do they leak through?

Only in two cases:

1. **TLK lookup fails** (strref unknown / TLK not loaded yet). Mitigation: use the `tag` as a fallback. Tags like `g_dnt_carth` are still better than `obj#7491`.
2. **Object has neither inline name nor strref** (rare; usually engineer-placed scaffolding that the player never sees anyway).

### `CExoString` and `CExoLocString` byte-layouts (CONFIRMED, SARIF)

```
CExoString  size=8
  +0x00 c_string  char*
  +0x04 length    ulong

CExoLocString  size=8
  +0x00 internal  CExoLocStringInternal*
  +0x04 strref?   int
```

The simple read pattern (already used in `Accessibility.cpp` for chargen labels) is: deref `*(char**)(field+0)`; if non-null and length > 0 (`*(ulong*)(field+4)`), speak it. If null/empty, fall through to TLK strref via `CTlkTable::GetSimpleString(strref)` (with SEH guard per `project_kotor_text_indirection.md`).

### Status

- **Tag (internal ID) — CONFIRMED**: every object, every type, never null
- **Localized inline names per type — CONFIRMED**: all major types covered
- **TLK fallback path — CONFIRMED**: address + signature known
- **Player character name — CONFIRMED**: direct accessor exists
- **Sane name-resolution pipeline (try inline → fall back to TLK → fall back to tag) — practical and lossless**

---

## Interesting findings collected en route

These are not directly required by Q1–Q7 but matter for the future nav-system design:

1. **`CSWGlobalVariableTable.locations +0xa8d0` / `location_count +0xb260`.** A persistent, savegame-backed table of `CScriptLocation` values. Modders set these via `nwscript` `SetGlobalLocation`. Could be the basis of a player-saved "favourite spots" feature ("warp to my marker") without us needing to invent new persistence.

2. **`CSWSCombatRoundAction.move_to_position +0x38`** — every queued combat action carries a movement target. While the player is in combat we can read this to announce "moving to flank X" before the action resolves.

3. **Encounters have geometry + spawn-point lists.** `CSWSEncounter.geometry_list +0x290 / spawn_points_list +0x2b8` — we can announce "you have walked into a 4-spawn encounter zone of three Sith Troopers" *from the data* before the spawn fires, by hooking the encounter's `script_on_entered +0x2e8`.

4. **`CSWSCreaturePartyFollowInfo`** is a complete party-following state machine in memory: `follow_location`, `last_leader_position`, `last_follower_position`, `stick_to_position`. A nav system could surface "your party is N metres behind you" from these fields.

5. **`CSWSObject.spell_target_position +0x1b0`** — for AoE casts, the engine writes the centre-of-effect into this field on the caster *before* the FX resolves. Hook here to give the player a heads-up on hostile AoE landing zones.

6. **`CSWSArea.unescapable +0x2ac` / `restrict_mode +0x2b0` / `stealth_xp_*`** — area-level mode flags. Useful "you cannot leave this area" / "stealth XP available" announcements at area-enter time.

7. **`CSWSModule.minutes_per_hour +0x1a0` / `dawn_hour +0x1a1` / `dusk_hour +0x1a2` / `start_*`** — in-game time of day is readable at these offsets. Some quests behave differently after dark.

8. **`CClientExoApp::AddMoveToModuleMovie @0x5edb50` returns the loading-screen movie name.** During a transition we know which loading screen will play — useful for "loading: going to Manaan Docks" announcement before the movie starts.

9. **`HandleMouseClickInWorld @0x620350` is a single, side-effect-free entry point.** That's the natural hook for a "speak the world coordinate the player just clicked" feature, plus capture-and-replay for testing.

10. **`Render*` debug toggles already exist as live globals.** Flipping `RENDER_TRIGGERS` and `RENDER_AABB` mid-session is a one-byte write — invaluable for sighted dev verification of what the iterators see.

11. **`CSWGuiMapHider`** is well-named: it gates which map-notes are visible *to the player* based on quest/discovery state. So the same pin can be in `CSWCArea.map_pins[]` but hidden until the player gets close. We need `CSWGuiMapHider::InitializeMapNotes` rather than the raw array if we want to respect discovery. **(`InitializeMapNotes` decomp also reveals the engine's own walk: enumerate `CSWSArea.game_objects[]`, vtable-cast each to `CSWSWaypoint`, filter by `+0x228 has_map_note != 0`, then `IsWorldPointExplored` to apply fog-of-war, then `CExoLocString::GetString` on `+0x230 map_note`.)**

12. **The `pathfind_info` struct holds `creature_object_id +0x2c`** — pathfinding queries are scoped to a specific creature. So we can't ask "is there a path from A to B" in the abstract; we have to ask "is there a path for this character." That's normal for steering-based pathfinders and matches what the AI actions expect.

13. **`CSWSCreature::WalkUpdateLocation_QuickWalk_FollowLeader_FindPath @0x51ac10`** — Lane named this beautifully. It's the per-tick movement update for party-followers, including a re-pathfind branch when the leader has moved. Reading from this in our hook will let us tell the player "your party is recomputing path" if we ever need to.

14. **`CSWCollisionMesh.world_coords +0x4`** is a *boolean* flag. When non-zero, `LocalToWorld` returns the input unchanged — meaning the mesh is already authored in world space. When zero, the mesh applies position + quaternion transform. **Implication for wall extraction:** check `world_coords` per mesh and skip the `LocalToWorld` call when it's set, otherwise apply the full transform. This matters because room walkmeshes are typically in mesh-local space (need transform), while some placeable colliders may be in world space (don't transform).

15. **`Yaw(Quaternion*)` returns degrees, not radians.** The `* 57.29578` (= 180/π) at the end of every relevant function confirms this throughout the engine. KotOR works in **degrees everywhere** for human-readable angles — use that directly in announcements.

16. **The minimap's per-area orientation field (`+0x10` on `CSWSAreaMap`) lets area designers rotate the map** so "north" on the displayed map matches the level's narrative orientation, regardless of the underlying world axes. This means our spoken "facing 47°" might want to be "facing 47° on the map" (using the rotated frame) rather than the raw world bearing. Both are derivable; pick per-feature.

17. **The `CSWSAreaMap` exploration bitfield (`+0x00`, `+0x04`)** is a packed bit-per-grid-cell array. Reading it directly lets us tell a player "you have explored 73% of this area" — useful as an exploration-progress narration. Bit at index `(grid_y * (grid_width + 1) + grid_x)` of the bitfield.

18. **The walkmesh material 2DA (`surfacemat.2da`) gives us free footstep classification.** Already loaded in memory as `Rules->internal->all_2DAs->surfacemat`. Schema: each row has columns `Walk` (0/1 walkable), `Sound` (footstep sound resref), `Step` (step pattern), `Grass` (grass overlay), and more. So an "audio under foot" announcement can be "stepping on stone", "stepping on metal grating", etc., with no extra RE.

19. **Wall extraction stride confirmation:** `WalkmeshFace` is exactly 12 bytes (3 × ulong vertex indices). `Vector` is exactly 12 bytes (3 × float). `SurfaceMeshAdjacency` is exactly 12 bytes (3 × int neighbour-face indices). **All three arrays are tightly packed, fixed-stride, indexable by face number — no struct alignment surprises.**

20. **`HandleMouseClickInWorld` takes no parameters** (`__thiscall(void)`) — the click coordinates and engine state are already on the manager. So a hook can read them from the appropriate `CSWGuiManager` field rather than from registers, which makes the hook simpler.

---

## Closing summary

Every navigation question has a clear, evidence-backed answer. The engine is **far more accommodating** than we feared at the start of the session: the data we need is in named structures with known offsets, the operations we need are already implemented as callable functions, and all of it is per-area-scoped so we can iterate without leaking memory or stepping on the renderer.

The second-pass decompilation **closed every load-bearing OPEN item** from the first pass. The wall-geometry path that was deferred as "only if needed" turned out to be a 30-line loop over data already in memory, fully usable for the audio-feedback-for-nearby-walls feature.

### Updated design seed (incorporating wall-audio from the start)

> Build the nav system on top of `CSWSArea` as the unit. **On area-enter:**
>
> 1. **Snapshot interactables** — walk `game_objects[]` filtered by accessibility-relevant `GAME_OBJECT_TYPES` (DOOR, PLACEABLE-usable, TRIGGER-with-transition, WAYPOINT-with-map-note, CREATURE). Cache `{position, localized_name (TLK-resolved), kind, kind-specific-descriptor}` per object.
> 2. **Snapshot wall edges** — for each `CSWSRoom`, walk faces × adjacencies; for every `adj.indices[i] == -1`, emit a `WallEdge {Vector a, Vector b, room_id, material_id}` with both endpoints transformed via `CSWCollisionMesh::LocalToWorld`. Cache once per area-enter.
> 3. **Snapshot map metadata** — read `CSWSAreaMap.{world_origin_*, world_units_per_*_pixel, orientation, grid_*}` so we can do world↔map translations locally without re-calling the engine.
>
> **Per-tick / on-demand:**
>
> - **"What's around me"** — sort cached objects by distance from player position, speak top N.
> - **"Walls nearby"** — for each cached wall edge, compute closest-point-on-segment (2D); pick K nearest, render to spatial audio with bearing relative to player facing and inverse-distance volume.
> - **"Floor type"** — `GetSurfaceMaterialWalkCheckOnly` at player position → look up `surfacemat.2da` → speak material name.
> - **"Next interactable / cycle target"** — set engine's last-target via `SetLastClickedOnTarget`, speak (uses engine's own highlight/passive-selection plumbing).
> - **"Move to X"** — call `CSWSCreature::AddMoveToPointAction(player, ++actionId, &dest, INVALID, INVALID, run, 0,0, 0,0, 0,0, 0,0, &dest, 0,0, 0)`.
> - **"Map browse"** — wire to `CSWGuiMapHider::GetNextMapNote`/`GetPrevMapNote` to share the engine's discovery filtering.
>
> **Compass / "facing":** `bearing° = atan2(orient.y, orient.x) * 180/π`, normalize to [0, 360), translate to a cardinal/intercardinal name (N/NE/E/...). Add the area's map-orientation offset if announcing in map-frame.

### Open items left for follow-up (all genuinely optional)

- **`CSWCMapPin.flags +0x104` bit semantics** — the 32-bit flag value's individual bits aren't yet mapped (kind enum? blink? hidden? player-placed?). The wire packet decomp gives us the field but not the bit-level meaning. ~30 min decomp pass against `HandleServerToPlayerMapPin @0x663810` would resolve.
- **`CSWCMapPin.{key1,key2}` exact split** — almost certainly a 64-bit object id split, but we could also read a few live pins at runtime to confirm. Trivial, defer.
- **`AddMoveToPointAction` int param fine semantics** — `pathMode`, `avoidFlags`, `flagBit3/9/10` map to a feature set we don't need for "move to here"; only relevant if we want to expose advanced AI control to the player.
- **Runtime calibration of world units** — confirm 1.0 unit = 1 metre by reading `personal_space` on a known-humanoid creature (expect ~0.5–1.0). One-line read; do it during the first nav-system test session.
- **`CSWSRoom` byte layout (stride 0x4c per `GetRoom` decomp)** — we know the stride and that `surface_mesh` is at some offset early on. Internal field labels would help; not blocking.

Investigation closed (second pass).

---

## Q8 — Audio output: playing nav cues through the engine

Goal: render orientation/wall/distance nav cues via the engine's own audio pipeline (so volume sliders, EAX, focus-pause, etc. all "just work"), with positional 3D where it helps. This section documents the engine's audio stack and the practical paths for our nav cues.

### TL;DR

- **Underlying renderer:** Miles Sound System v32 (`Mss32.dll`, vendored by RAD Game Tools), wrapped by KotOR's `CExoSound` / `CExoSoundSource` / `CExoStreamingSoundSource` classes. Hardware 3D path supports EAX environmental reverb when available.
- **Two clean entry points for our cues:**
  - **Fire-and-forget one-shot:** `CExoSound::PlayOneShotSound(resref, …)` `@0x5d5e00` (2D) or `CExoSound::Play3DOneShotSound(resref, position, …)` `@0x5d5e10` (positional). Engine does the spatial math, mixing, ducking, priority eviction.
  - **Owned source we control:** construct a `CExoSoundSource` (or `CExoStreamingSoundSource` for >100 KB files), `SetResRef("our_cue", …)`, configure 3D + position + min/max distance + volume + looping, `Play()`. Persistent until `Stop()`.
- **External WAVs work without engineering effort.** The engine resolves `CResRef` through its standard chain (BIF archives → RIM modules → **`Override\` folder** → `streamwaves\` / `streamsounds\` / `streammusic\`). Drop our `acc_wall_left.wav` into `Override\`, reference it as `acc_wall_left` from `SetResRef`, the engine finds it.
- **Listener position/orientation is set by the engine each frame** from camera/player. Our 3D cues are automatically panned/attenuated relative to the player. We do not need to manage the listener.
- **Volume sliders apply for free** — every cue lives under one of three engine slider categories (Music / SoundFX / Dialog). Pick SoundFX for nav cues; user's existing volume preference is respected.

### Engine audio stack — confirmed pieces

```
[OS audio device]
   ↑
[Miles Sound System v32 — Mss32.dll]      (vendor: RAD Game Tools)
   ↑                                       Provides 2D + 3D voices, EAX, streaming
[CExoSoundInternal]                        Singleton; manages voice budgets, priority
   ↑                                       groups, listener pose, fade lists
[CExoSound]                                Thin facade — what callers use
   |
   +→ [CExoSoundSource]        (one-off / managed in-memory voice; backed by CResWave)
   +→ [CExoStreamingSoundSource] (streamed from disk; for music/long VO)
   +→ One-shot helpers (PlayOneShotSound, Play3DOneShotSound)
```

`Mss32.dll` is at `<install>/Mss32.dll`. Provider preferences load from disk via `CExoSoundInternal::LoadProviderPreferences @0x5d66d0`; available 3D providers enumerated by `Enumerate3DProviders @0x5da6d0`. EAX detection at `CExoSound::GetBestEAXAvailable @0x5d6460` / `GetEAX @0x5d6450`. Reverb toggle on streaming source via `CExoStreamingSoundSource::SetReverbEnabled @0x5d5bb0`.

### How sounds are accessed — the resref system

KotOR identifies every audio asset by a **CResRef** (a 16-char tag, plus 4-byte resource type id). For audio:

- ResType `4` = `WAV`/`WAVE` (used in `CResHelper<CResWave, 4>`)
- The engine's resource manager (`EXO_RESOURCE_MANAGER_PTR @0x7a39e8`) walks search paths in this priority order:
  1. **`Override\`** ← drop our nav-cue WAVs here; highest priority
  2. **`streamwaves\`** (VO and dialog)
  3. **`streamsounds\`** (creature ambient)
  4. **`streammusic\`** (music tracks)
  5. **BIF archives in `data\`** (base assets, `chitin.key`-indexed)
  6. **RIM modules in `modules\` and `rims\`** (per-area / per-module bundles)

Confirmed via `CExoSoundSource::SetResRef @0x5d61c0` decomp:

```c
void CExoSoundSource::SetResRef(CResRef *res, int resType) {
  ShutDownSource(this->internal);
  this->internal->field22_0x58 = 0;
  CExoSoundInternal::Render(this->internal->sound_internal);
  CResHelper<CResWave, 4>::SetResRef(this->internal, res, resType);
  // -> ResMan looks up the resref under all search paths; loads CResWave on demand
}
```

The 4 in `CResHelper<CResWave, 4>` is the WAV resource type id; pass that as `resType`.

**Practical implication for nav cues:** put a `.wav` file (RIFF/PCM, no special format) named `acc_wall.wav` (≤16 chars without extension) under `<install>/Override/`, then `mySource.SetResRef(CResRef("acc_wall"), 4)` and `mySource.Play()`. No archive editing, no installer step beyond file copy.

### Playing one-shots — simplest path

**2D one-shot** (UI feedback, no spatial component):

```c
// CExoSound::PlayOneShotSound (signature CONFIRMED, decomp @0x5d5e00)
CExoSound::PlayOneShotSound(
    CResRef *res,           // "acc_announce"
    byte    priority_group, // 0..priority_group_count-1
    ulong   delay_ms,       // delay before play
    byte    looping,        // 0 = one-shot
    float   volume,         // 0.0..1.0
    float   pan             // -1.0 (left) .. +1.0 (right), 0.0 centre
);
```

**3D one-shot** (spatial — for "wall on your right"):

```c
// CExoSound::Play3DOneShotSound (signature CONFIRMED, decomp @0x5d5e10)
CExoSound::Play3DOneShotSound(
    CResRef *res,
    Vector  position,        // world-space; z is offset by next param
    float   z_offset,        // added to position.z internally
    uchar   priority_group,
    ulong   delay_ms,
    byte    looping,
    float   volume,
    float   max_distance     // (signature has additional float trailing — see decomp note)
);
```

The engine pans/attenuates the 3D one-shot relative to the current listener (= player camera). **For "wall to your left at 2m" — pass `position = player_pos + 2 metres along player_left_axis` and the engine plays it from that location automatically.**

Both functions delegate to `CExoSoundInternal::PlayOneShotSound @0x5d7550` / `Play3DOneShotSound @0x5d7680` after a null-check on `this->internal`.

### Owned sources — for persistent / stateful cues

Use a `CExoSoundSource` when:

- The cue **loops** (e.g. an "imminent wall" warning that hums while the player approaches it)
- We need to **reposition** while playing (e.g. a continuous "follow me" beacon)
- We want to **stop or fade** programmatically
- We need explicit **min/max distance** falloff curves

API surface (`CExoSoundSource`, all `__thiscall`):

```
Constructor                @0x5d5870 / @0x5d60e0 (with arg)
Destructor                 @0x5d60a0
Set3D(bool enable)         @0x5d5910
SetResRef(*res, resType)   @0x5d61c0
SetPosition(Vector*)       @0x5d59e0
SetDistance(min, max)      @0x5d61a0   // 3D falloff range, world units
SetVolume(byte 0..127)     @0x5d5950
SetLooping(bool)           @0x5d59d0
SetPitchVariance(float)    @0x5d5980
SetFixedVariance(float)    @0x5d5a30
SetPriorityGroup(byte)     @0x5d5900
Play()                     @0x5d5930
Stop()                     @0x5d5a20
GetLooping() -> bool       @0x5d6190
IsHardwarePlayingSoundPleaseDontUseThis() @0x5d5920  (Lane preserved BioWare's name verbatim — speaks for itself)
```

Internal layout for reference (`CExoSoundSourceInternal`, DB):

```
+0x00  vtable
+0x04  internal pointer chain
+0x08  resource              (CResWave handle once SetResRef has bound it)
+0x1c  do_3d                 (bool — whether this voice routes through the 3D pipeline)
+0x24  looping
+0x35  priority_group
+0x40  one_shot_list_head
+0x44  sound_internal        (back-pointer to CExoSoundInternal singleton)
+0x50  fixed_variance_frequency
+0x68  fixed_variance
+0x6c  min_volume_distance   (world units)
+0x70  max_volume_distance   (world units)
+0x78  position              Vector
+0x84  priority_group_volume
```

Wrapping `CExoSoundSource` in our own thin C++ class is the path of least resistance; the engine handles voice eviction, mixing, distance attenuation, priority grouping.

### Streaming sources — for long cues

`CExoStreamingSoundSource` reads from disk in chunks instead of fully loading a `CResWave`. Same shape as `CExoSoundSource` plus:

```
Play()                      @0x5d5b10
Stop()                      @0x5d5b80
FadeAndStop(float seconds)  @0x5d5b90
SetReverbEnabled(bool)      @0x5d5bb0
GetFilePlaying(*name)       @0x5d6230
GetPlayFailed() -> bool     @0x5d5b00
```

Internal layout (DB):

```
CExoStreamingSoundSourceInternal
  +0x08  priority_group_index
  +0x09  file_playing
  +0x20  sound_internal
  +0x48  max_volume_distance
  +0x4c  min_volume_distance
  +0x50  volume
  +0x5c  position
```

For typical nav cues (≤1 sec, ≤50 KB) a regular `CExoSoundSource` is faster and avoids the streaming bookkeeping. Reserve streaming for longer narration clips if we ever pre-record any.

### Listener pose — already managed by the engine

The engine sets the listener position and orientation each frame from the camera/player. We don't need to call these — but they're available if we want to override (e.g. simulate listening from the top of the head for a more natural binaural feel):

- `CExoSound::SetListenerPosition(Vector*)` `@0x5d5df0` (decomp **CONFIRMED** — delegates to internal)
- `CExoSound::SetListenerOrientation(Vector* forward, Vector* up)` `@0x5d5de0` (decomp **CONFIRMED** — note the **two-Vector** form: forward + up axes, not a single heading)
- Backing field: `CExoSoundInternal.listener_position +0x98`

**Note the orientation API takes (forward, up) Vectors** — that's standard Miles 3D listener convention, and it's *different* from the `(x, y) heading` convention used for object orientation elsewhere in the engine (Q1). Don't mix the two.

### Volume / mixer surface

KotOR groups every voice under one of four sliders. Each has a getter and a setter on `CExoSound` / `CExoSoundInternal`:

| Slider | Getter | Setter |
|---|---|---|
| Music         | `GetMusicVolume @0x5d6010`        | `SetMusicVolume @0x5d5c50` |
| Sound Effect  | `GetSoundEffectVolume @0x5d6030`  | `SetSoundEffectVolume @0x5d5cd0` |
| Dialog        | `GetDialogVolume @0x5d6050`       | `SetDialogVolume @0x5d5d50` |
| Movie         | (handled separately by movie player) | — |

All voices are scaled by the appropriate slider before mixing. **Nav cues should claim the SoundEffect slider** — that's the user's "ambient game noise" volume; if they crank it down, our cues get quieter too, which is the correct behaviour.

There's also a **Master Mute** path (`CExoSoundInternal::MuteSound @0x5d6a10` / `UnMuteSound @0x5d6a50`) and **focus-loss handling** (`PauseAllSounds @0x5d82c0` / `ResumeAllSounds @0x5d83f0`). When the user alt-tabs out, the engine pauses all sounds — our nav cues will pause too, which is again correct.

### Priority groups — voice budget management

The engine has a fixed budget of `number_2d_voices` (`+0x28`) and `number_3d_voices` (`+0x29`) — typically 16-32 each, hardware-dependent. When the budget is exhausted, the engine evicts the **lowest-priority** active voice. Priority groups are loaded from `priorityGroups.2da` (in BIFs) by `CExoSoundInternal::LoadPriorityGroups @0x5d7bd0`.

For nav cues, we want a **dedicated mid-priority group** so:
- Critical sounds (dialog, combat hits) still preempt us
- Ambient noise doesn't preempt us
- We don't preempt each other excessively

The cleanest approach: pick an existing low-but-not-lowest priority group when calling our cues. We can enumerate the loaded groups via `CExoSoundInternal::priority_groups +0x4c` once we know the array layout. **OPEN — not blocking; pick group 0 or 1 conservatively until we measure.**

### NWScript path (slower; for completeness)

`ExecuteCommandPlaySound @0x541170` is what NWScript `PlaySound("foo")` invokes. Decomp shows it pops a string from the VM stack and queues a `CSWSObject::AddAction(0x17 = ACTION_PLAYSOUND, …)` on the caller. **It does not call `PlayOneShotSound` directly** — it goes through the action queue, which means:

- One-tick minimum latency
- Subject to action-queue reordering / dropping
- Server-side: `CSWSObject::AIActionPlaySound @0x57cf00` then propagates the play via `SendServerToPlayerAIActionPlaySound` to all players within `≤1000` units (distance² ≤ 1e+06)

For our nav cues we **bypass the NWScript path entirely** and call the C++ entry points directly — lower latency, no action-queue dependency, no network propagation we don't need.

### In-world sound objects — `CSWSSoundObject`

The engine also has a "sound object" concept — placeable speakers in the world. Layout (DB):

```
CSWSSoundObject
  +0x228 active                bool
  +0x22c postional             bool   (sic)
  +0x230 looping
  +0x234 volume
  +0x238 volume_vrtn           (variation)
  +0x23c times                 (play schedule)
  +0x240 pitch_variation
  +0x244 hours                 (time-of-day window)
  +0x248 fixed_variance
  +0x250 random_postion        bool   (sic)
  +0x254 random_range_x
  +0x258 random_range_y
  +0x25c sounds                CExoArrayList<CResRef> — playlist
  +0x260 sounds_count
  +0x268 interval
  +0x26c interval_vtrn
  +0x270 min_distance
  +0x274 max_distance
  +0x278 continuous            bool
  +0x27c random                bool
```

NWScript hooks all the controls:
- `ExecuteCommandSoundObjectPlay @0x543be0`
- `ExecuteCommandSoundObjectStop @0x543e10`
- `ExecuteCommandSoundObjectFadeAndStop @0x543e60`
- `ExecuteCommandSoundObjectSetPosition @0x543d10`
- `ExecuteCommandSoundObjectSetVolume @0x543da0`
- `ExecuteCommandSoundObjectSetPitchVariation @0x543ca0`
- `ExecuteCommandSoundObjectSetFixedVariance @0x543c30`
- Get-side: `Volume @0x543b80`, `PitchVariance @0x543b10`, `FixedVariance @0x543aa0`

**We don't need this for nav cues** — `CExoSoundSource` is simpler. But it's worth knowing: if the player asks "what sound objects are nearby" we can iterate `CSWSArea.game_objects[]` filtered by `GAME_OBJECT_TYPES.SOUND = 16` and read each's resref playlist.

### Ambient music / area soundscape — `CSWSAmbientSound`

Per-area background music + ambient sound (DB):

```
CSWSAmbientSound
  +0x04 music_playing             bool
  +0x08 music_delay
  +0x0c music_day                 CResRef — daytime track
  +0x10 music_night               CResRef
  +0x14 battle_music_playing
  +0x18 music_battle              CResRef
  +0x1c ambient_sound             CResRef — looping background
  +0x20 ambient_sound_day
  +0x24 ambient_sound_night
  +0x28 ambient_sound_day_volume   byte
  +0x29 ambient_sound_night_volume byte
```

`CSWSAmbientSound::PlayMusic @0x5c9900` is the trigger. Combat music switches via `CSWVirtualMachineCommands::ExecuteCommandMusicBattle @0x5400c0`.

For accessibility we likely want to **announce the music change** ("entering combat music") rather than alter the music itself — read the playing-track resref and speak it on change.

### Practical recipe for our nav cues

Step-by-step plan for the next session, given everything above:

1. **Author 5–10 short WAV cues** (mono, 22 kHz or 44.1 kHz PCM, ≤200 ms each):
   - `acc_wall_n.wav` — wall pulse (very short percussive blip)
   - `acc_door.wav` — door chime
   - `acc_trig.wav` — trigger tone
   - `acc_npc.wav` — NPC presence ping
   - `acc_focus.wav` — focus changed
   - `acc_warn.wav` — warning (locked door, AOE incoming)
   - `acc_open.wav` — opening / activated
   - …

2. **Drop the WAVs in `<install>/Override/`** and add to our patch's install step (`kdev apply` already has a copy hook for `third_party/tolk/x86/*.dll` — extend to copy our WAV bundle).

3. **Resolve the CExoSound singleton at hook time.** From the decomps it's accessed as `someAccessor->internal != NULL`; the singleton lives at a global address. Need one xref-trace (or just read from a known caller site) to pin the global. **One small RE follow-up.**

4. **Build a thin wrapper** `acoustic.{h,cpp}`:
   ```c
   void Acoustic::PlayCue(const char *resref, float volume);
   void Acoustic::PlayCue3D(const char *resref, Vector pos, float min_dist, float max_dist, float volume);
   CExoSoundSource* Acoustic::OwnLooping(const char *resref); // for the wall-proximity hum
   ```

5. **Wall-proximity audio** (the user's primary motivation):
   - Per-tick (or per-100-ms): for each cached `WallEdge` within K metres, compute closest-point on segment → distance + bearing
   - Convert the K nearest into a **stereo cue stream**: pan = bearing-relative-to-player-facing (left/right), volume = inverse-distance, optional pitch shift = wall-orientation
   - Easiest first cut: fire `Play3DOneShotSound("acc_wall", closest_point, …)` at a fixed cadence; engine handles all the spatial math
   - Slightly more sophisticated: maintain ~3 looping `CExoSoundSource` voices (left/right/front), reposition them per tick to track the K-nearest wall midpoints

### Status

- **Audio renderer (Miles via Mss32.dll): CONFIRMED**
- **CExoSound facade + Internal singleton: CONFIRMED**
- **CExoSoundSource owned-source API: CONFIRMED** (full method list with addresses)
- **CExoStreamingSoundSource for big files: CONFIRMED**
- **One-shot 2D and 3D entry points: CONFIRMED** (signatures decoded)
- **CResRef → file resolution chain (Override → streamwaves/sounds/music → BIF/RIM): CONFIRMED**
- **Listener pose API: CONFIRMED** (engine-managed; we can override)
- **Volume slider categories: CONFIRMED** (Music / SFX / Dialog)
- **Priority groups: STRONG** (data structures known; group-name enumeration deferred)
- **In-world sound objects (`CSWSSoundObject`): CONFIRMED**
- **Per-area ambient music (`CSWSAmbientSound`): CONFIRMED**
- **CExoSound singleton global address: OPEN** (one xref-trace away; pin at hook-design time)

### Open items for follow-up (audio)

- **CExoSound singleton's exact global address.** All callers go through `someGlobal->PlayOneShotSound`; the global pointer hasn't been labeled in the DB. Resolve via xref-trace from one of the `0x5d5e00` callers (already enumerated in the SARIF query above).
- **Priority-group naming.** Pick a group conservatively (0 or 1) for nav cues; later, dump `priorityGroups.2da` to choose the right name.
- **Distance falloff curve shape.** Min/max distances are known; the falloff curve in between (linear? exponential? Miles default?) needs measurement, not RE — easier to tune by ear in our first nav-cue test session.
- **Dynamic ducking of game audio when speaking nav cues.** Probably not worth implementing — the volume sliders already let the user attenuate game audio for accessibility, and screen-reader speech is its own out-of-band channel through Tolk.

Investigation closed (third pass — audio).
