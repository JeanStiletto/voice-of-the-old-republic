# engine_app.h (69 lines)

The AppManager resolve seam — the root every engine object walk starts at, and
the single place the chain is written down.

```
*kAddrAppManagerPtr → CAppManager → +0x4 CClientExoApp  (UI / client side)
                                  → +0x8 CServerExoApp  (world / AI truth)

CClientExoAppInternal → +0x18 CSWCModule → +0x40 Camera
```

Each facade is an 8-byte public shell (vtable@0, internal@4); the `*Internal`
carries the real state. Which side answers which question is in
`engine-objects-and-architecture.md`.

## Why it exists

Created by Phase 3 candidate B1, slice 1 (server chain). Before it, the walk
was hand-rolled at every site: six different names for the same `+0x8` hop,
three private copies of the AppManager pointer, and per-site SEH guards that
some callers remembered and others did not — which is exactly the F2 crash
class. `engine_subscreen.cpp` had a comment justifying its own duplicate
because the alternative was file-local. That comment is gone with the
duplicate.

## Contents

- **`kAddrAppManagerPtr`** — `0x007A39FC`. A `.data` global, so deliberately
  NOT passed through `acc::addr::R()` (R() covers `.text` only; see the
  `.data` section note in `engine_offsets_addresses.h`).
- **`kAppManagerClientAppOffset`** (`0x4`) / **`kAppManagerServerAppOffset`**
  (`0x8`) — the two facade hops.
- **`kClientExoAppInternalOffset`** / **`kServerExoAppInternalOffset`** (both
  `0x4`) — facade → internal, same shape on both sides.
- **`kClientInternalModuleOffset`** (`0x18`) / **`kCSWCModuleCameraOffset`**
  (`0x40`) — the client-side continuation into the world view.
- **`GetAppManager()`**, **`GetClientApp()`**, **`GetClientAppInternal()`**,
  **`GetClientModule()`**, **`GetCamera()`**, **`GetServerApp()`**,
  **`GetServerAppInternal()`** — all SEH-guarded, all yield nullptr on a null
  link or a fault. A caller that goes on to CALL an engine function through
  the result still needs its own `__try` around that call: the guard covers
  the walk, not what you do with it.

The GUI continuation lives one level up, in `engine_panels.h`:
`ResolveGuiInGame()` (client internal +0x40) and `ResolveMainInterface()`
(CGuiInGame +0x90).

## K2 port

These constants plus the functions are the whole seam. On a KOTOR 2
executable the global's address changes (and possibly the hops); nothing else
in the codebase needs to know. This is the file to change.

## Consumers

Server chain (slice 1): `engine_area.cpp` (GetServerObjectArray),
`engine_area_map.cpp` (4 sites), `engine_reads_items.cpp` (3 sites),
`engine_player_party.cpp` (GetServerPartyTable), `engine_subscreen.cpp`,
`tutorial_popup.cpp`, `minigame_swoop_race.cpp`.

Client chain (slice 2): `engine_player.cpp` (GetPlayerServerObject + both
camera readers), `engine_player_party.cpp` (3), `engine_player_inputlock.cpp`
(GetPlayerControl), `engine_options.cpp` (GetClientOptions),
`engine_panels.cpp` (ResolveGuiInGame — which gained a guard it never had),
`engine_panels_state.cpp` (3), `engine_area.cpp` (3), `combat.cpp`,
`combat_diag.cpp`, `examine_view.cpp`, `menus_journal.cpp`,
`passive_narrate.cpp`, `minigame_aim.cpp`, `engine_subscreen.cpp` (2).

`engine_player.h` includes this header so its own includers keep seeing
`kAddrAppManagerPtr` unchanged.

GUI chain (slice 3): `engine_panels.cpp` owns the two resolves;
`engine_radial.cpp`, `engine_actionbar.cpp`, `engine_picker.cpp` and
`combat_diag.cpp` consume them. Twelve duplicated functions and seven
duplicated offset constants were deleted here.

Camera chain (slice 4): `engine_player.cpp` (GetCameraPosition,
GetCameraYawRadians), `camera_orient.cpp`, `probe_camera_distance.cpp`,
`probe_camera_state.cpp`.

**Nothing hand-walks the chain any more.** After slice 4 there is exactly ONE
dereference of `kAddrAppManagerPtr` in the codebase — `GetAppManager()` in
`engine_app.cpp` — and zero uses of the hop constants outside this file.
That invariant is worth re-checking with a grep before adding a new engine
read.
