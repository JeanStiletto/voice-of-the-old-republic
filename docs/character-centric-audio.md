# Character-centric audio — investigation snapshot

**Status: PARKED 2026-05-13.** Probe-only state landed. Two viable
approaches identified, Option B's mechanism verified working in-game,
neither shipped as a feature yet. Revisit when the user has a better
play-feel sense for which mode is right and how much audio improvement
the change actually delivers in scenarios that originally felt bad.

## Problem statement

Engine 3D audio listener follows the camera (verified Phase 1 lay-off 4
gate; documented in `audio_bus.cpp`). KOTOR's orbital camera trails the
character at ~3m and orbits around it on A/D. So:

- All engine native 3D audio (footsteps, NPC voice, ambient, combat) is
  panned + attenuated relative to a point that translates and rotates
  with camera orbit, **not** relative to the character.
- Our own cues compensate via `PlayCue3D` shifting source positions by
  `(camera − character)` — character-relative for *our* cues only.
- User-perceived effect: "audio off when turning" — the engine native
  sounds yaw around the player whenever A/D orbits the camera.

Two options to make engine native audio character-relative.

---

## Option B — collapse camera distance to ~0

### Mechanism

Listener follows camera (engine-owned). If camera position equals
character position, listener is at character. No detour needed; just
per-tick write to the orbit-radius field.

### Known surfaces (verified)

- **Chain to active behavior:**
  ```
  *kAddrAppManagerPtr → AppManager
    +0x04             → CClientExoApp
      +0x04           → CClientExoAppInternal
        +0x18         → CSWCModule
          +0x40       → Camera*
                        Camera::vtable[0x80](-1) → CAurBehavior* (active)
  ```
- **Active behavior** for free-roam: `CSWCameraOnAStick` (other
  behaviors: `CSWBehaviorCameraCombat`, `CSWBehaviorCameraDialog`,
  `CSWCameraFreeLook`, `CSWCameraDeath`, `CSWCameraNavigate`).
- **Target orbit radius:** `CSWCameraOnAStick + 0x110` (Ghidra label
  `field46_0x110`). Used directly in `Control_ComputeDesiredPosition
  @0x00637af0` as `Vector(0, distance, 0)` rotated by the orientation
  quaternion at +0x28..+0x37.
- **Camera Z-offset (height bias):** `CSWCameraOnAStick + 0x120`
  (`field50_0x120`), default 0.45m.
- **Auto-fit recompute flag:** `CSWCameraOnAStick + 0x84`
  (`field29_0x84`). When non-zero, `Control` recomputes `field46_0x110`
  every frame from view-cone math. **Default is 0** — recompute branch
  isn't active in normal play, so direct distance writes survive
  frame-to-frame. Confirmed by probe (`autoFitPre=0` every tick).
- **Engine setters worth knowing:**
  - `CSWCModule::ZoomCamera @0x006401d0` — adds delta to the deep
    inner-state distance (writes both inner state +0x34 and the
    `CSWCModule.field32_0x9c` cache).
  - `CSWCModule::AcclTurnCamera @0x00640090` — same shape, yaw at deep
    inner state +0x40, cached on `CSWCModule.field31_0x98`.
  - `CSWCModule::AcclTiltCamera @0x0063fdb0` — pitch sibling.
- **Engine readers:**
  - `Camera::GetDist @0x0045c1d0`, `Camera::GetYaw @0x0045c170`,
    `Camera::GetPitch @0x0045c1a0` — all null-check `behavior::vtable[7]()`
    return; bail to 0 if null. *Caveat:* in probe runs the vtable[7]
    return was observed null (`state=00000000`), so `GetDist` returned 0
    even though `field46_0x110` was a valid 3.2m. **Do not use these
    accessors as primary readers** — read `field46_0x110` directly.
- **Camera tuning globals:**
  - `cameraPersonalSpace @0x007a241c` — float, default 0.35m. Added in
    `Control_HitCheckCamera @0x0063b050` as `(cameraPersonalSpace +
    0.15) × ratio` to bump the post-collision camera position out from
    the character. **Effective minimum camera-character distance is
    ~0.5m even when target distance is 0** — this is the secondary
    clamp.
  - `cameraInterpAmt1 @0x007a2430` — 2.5, current/target lerp factor.
  - `cameraInterpDistAmt @0x007a243c` — 0.5, distance smoothing.
  - `cameraFreeStyle @0x00833938` — non-zero short-circuits `Control`
    to debug `CameraFreeStyleControl`.
- **Pipeline:** `CSWCameraOnAStick::Control @0x0063bcb0` →
  `Control_UpdateCameraDesiredOrientation` →
  `Control_ComputeDesiredPosition @0x00637af0` (uses field46_0x110) →
  `Control_AccelerateInterp @0x00637b80` (smoothing) →
  `Control_ComputeNewCameraPosition @0x006398f0` →
  `Control_HitCheckCamera @0x0063b050` (adds personal-space buffer) →
  per-frame Gob position write.

### Probe results (2026-05-13)

- Direct write to `field46_0x110` survives frame-to-frame. Per-second
  rate summary across 50+ second runs at 0.0m and 0.5m showed
  `stomps=0` after the initial transition tick.
- Default `tgtDist=3.200`, `zOffset=0.450`, `persSpace=0.350`,
  `autoFit=0`. All as expected from the decomp.
- 0.0m clamp:
  - Writes stuck cleanly.
  - **`camera_announce` died** — it derives camera direction from
    `normalize(player − camera)`, and at distance 0 the vector falls
    below the 0.1m guard. Repeated `first-tick anchor` resets in the
    log. So A/D produced no spoken sector announcements.
  - Character did still rotate (TurnAnnounce kept firing on W press),
    but **engine's snap-character-to-camera-yaw uses the same
    degenerate vector**, so W-press direction was unpredictable.
- 0.5m clamp: writes stuck, `camera_announce` worked normally (sector
  transitions resumed). No control issues observed.
- 2.0m clamp: same as 0.5m.
- **Audio difference reported as "small gap" across all modes.** In the
  test session (Endar Spire corridor) the user could barely tell the
  modes apart by ear. Open whether different scenarios (Taris cantina,
  party of moving NPCs, combat) would expose more dramatic differences.

### What's still TODO for shipping

1. **Decide on locked distance.** Probe gave us viable mechanism; play
   needs to decide between 0.5m, 1.0m, 2.0m. 0.0m ruled out (breaks
   direction derivation in mod + engine). Consider whether zeroing
   `cameraPersonalSpace` to allow sub-0.5m effective distance is worth
   the visual oddness.
2. **Free-roam gate.** Release the clamp during:
   - Dialog (we already detect via `dialog_speech`'s panel monitor)
   - Cutscenes / animated camera (`CSWCModule::IsCameraAnimated
     @0x006412b0`)
   - Combat camera (CSWBehaviorCameraCombat active — but maybe leave it
     clamped during combat? Open design question)
   - Death camera (`CSWCameraDeath` active)
   - Computer dialog camera, placeable cameras
   Without this gate, cutscene shots break: every cinematic shot gets
   forced back to the clamped distance from the character.
3. **Lifecycle.** Re-anchor / reset on area transition? On load? On
   PlayerCharacter swap (Tab)? Test if changing leader during a clamp
   session does anything weird.
4. **Move the constant out of the probe.** Add to `core_settings` so a
   future options UI can surface it. The user might want to toggle
   character-centric audio on/off without rebuilds.
5. **Strip the per-second log spam.** Reduce to edge-triggered
   "clamp armed / released" lines for the production version.
6. **Drop the `field29_0x84` per-tick zero-write.** Log confirmed it's
   0 by default; the write is a paranoia line.
7. **Coordinate with `view_mode`.** View mode already does
   `SetPlayerInputEnabled(false)` and overrides the listener to the
   virtual cursor via `OnSetListenerPosition`. If a permanent distance
   clamp is also active, what happens? The listener-override path
   already handles cursor vs camera-position; needs check that it
   doesn't fight the clamp.
8. **Interaction with `PlayCue3D` character-offset compensation.** Our
   own cues currently shift source by `(camera − character)`. If
   listener is now effectively at character (camera at character), the
   offset becomes ~0 — no harm, but the code path is dead. Could be
   stripped, or left as defence-in-depth. **Decide before shipping.**
9. **Verify in target scenarios** before declaring it worthwhile:
   - Taris cantina chatter at 0.5m vs vanilla — does the audio feel
     less "swinging" when you A/D?
   - Walking past a moving companion / NPC — does their footstep audio
     match your spatial expectation better?
   - Combat with multiple enemies — does positional audio for enemy
     swings feel right?
   If "small gap" everywhere, shelve Option B.

### Files

- `patches/Accessibility/probe_camera_distance.{h,cpp}` — the
  diagnostic probe.
- Hotkeys: `Ctrl+F12` dump snapshot (kept bound),
  `Ctrl+F11` cycle clamp modes (currently unbound — rebind in
  `hotkeys.cpp` when re-investigating).
- `tools/ghidra-scripts/ListFunctionsByName.java` +
  `ListSymbolsByName.java` — new scripts for enumerating
  function/data symbols by substring; used to find
  `cameraPersonalSpace` and the camera-control function family.

---

## Option A — detour the engine listener

### Mechanism

Leave the camera alone; override listener pose at the engine's two
listener-write call sites to substitute character position + character
forward. Engine continues to compute audio relative to listener pose —
which now sits at the character — and the camera continues to look
camera-shaped on screen.

Audio effect should be identical to Option B at 0.0m (listener at
character), but without Option B's collateral damage on the W-snap
direction derivation (camera stays at 3m, so `player − camera` is well-
defined for the engine's internal use).

### Known surfaces (verified)

- **`CExoSound::SetListenerPosition @0x5d5df0`** — single xref from
  `CClientExoAppInternal::UpdateSoundEngine`. **Already detoured** as
  `OnSetListenerPosition`. Currently substitutes view-mode cursor
  position when active; passthrough engine value otherwise.
- **`CExoSound::SetListenerOrientation @0x5d5de0`** — takes
  `Vector* forward, Vector* up` (Miles 3D listener convention — two
  Vectors, not a single heading). **NOT yet detoured**; same hook shape
  as position.
- **Backing field:** `CExoSoundInternal.listener_position +0x98`.
- **Player heading source:** `CSWSObject + 0x9c` (Vector with z=0;
  `x = cos(yaw)`, `y = sin(yaw)`). Already read via
  `acc::engine::GetPlayerFacing`.
- **Player position source:** `CSWSObject + 0x90`. Already read via
  `acc::engine::GetPlayerPosition`.

### Open questions

1. **Does writing only the listener while leaving the camera moving
   normally cause audio glitches?** Reverb zones, occlusion volumes,
   area-sound transitions might be tied to listener position. If the
   engine has any "listener is in this room" boolean cached from the
   listener position, it might confuse itself when listener and camera
   diverge by ~3m.
2. **Do we still need an input-side change?** If listener is at
   character but A/D still rotates the camera (not character), the
   user hears the audio scene yaw (because the engine's listener
   orientation is camera-forward, not character-forward). So Option A
   without orientation override would only be a partial fix. Detouring
   `SetListenerOrientation` to use character-forward fixes that — but
   then A/D rotates camera (visual) without rotating listener
   (audio), which might feel disconnected.
3. **Interaction with `PlayCue3D` character-offset compensation.** If
   listener is at character, the offset compensation becomes
   double-compensation. Must be stripped or gated on "Option A
   active."
4. **Character rotation primitive — only needed if we go further than
   listener override.** If we want A/D to *also* rotate the character
   (so character keeps facing where the audio scene says "forward"),
   we need a way to rotate the player creature without going through
   the W-walk path. Currently we have:
   - Direct write to `CSWSObject + 0x9c` (heading vector) — works but
     doesn't update client cache (`CSWCObject + 0x30`) or rendering
     Gob; visual rotation may lag.
   - NWScript `SetFacing` (verb #10) — routes through action queue;
     too laggy for per-tick A/D.
   - Engine internal helper (W-press snap-character-to-camera-yaw
     path) — **address unknown**. Needs RE.
5. **Cutscene/dialog gate.** Same as Option B — but the gate is
   different: release the listener-override during cinematics so the
   engine cinematic listener (which may be set explicitly per shot)
   reaches the audio pipeline. Already proven: view-mode toggle in
   `OnSetListenerPosition` is the same gating shape.

### What's needed to ship

1. **Extend `OnSetListenerPosition`.** Add a free-roam state-check:
   if not in cutscene/dialog/view-mode, substitute character position
   for the engine's camera value. Otherwise passthrough as today.
2. **Detour `SetListenerOrientation @0x5d5de0`.** New hook, same shape
   as position. Substitute character-forward derived from
   `GetPlayerFacing` for the engine's camera-forward.
3. **Decide on input rebind.** Three options:
   - **Listener only.** A/D still rotates camera. Audio scene only
     yaws when character yaws (W press). Camera-relative input
     stays vanilla.
   - **A/D rotates character.** Need character rotation primitive
     (see open question 4). Audio yaws with A/D directly.
   - **Hybrid.** A/D rotates listener orientation directly without
     touching camera or character. Cheap, but creates a tri-frame
     mismatch (camera, character, listener all different).
4. **Strip / gate `PlayCue3D` character-offset compensation.**
5. **Re-bind `ProbeAudioFire` (plain F11)** for in-test validation —
   the fixed-North probe is the diagnostic for whether listener pose
   tracks character vs camera.
6. **Cutscene/dialog gate** — shared with Option B's TODO list.

---

## Decision criteria for revisit

Pick Option B if play-testing at 0.5m / 2.0m shows a clear audio
improvement in challenging scenarios (cantina, combat, NPCs nearby).
Cheaper to implement; one detour-less surface.

Pick Option A if Option B's audio improvement is too small to justify
the camera collapse, **or** if scenes get visually disruptive enough
that the user wants a normal-looking camera while still getting the
audio improvement.

Skip both and accept current behaviour if neither option's audio gain
is meaningful — and look for the real cause of "audio off when
turning" somewhere else (camera-orbit-arm motion during sustained A/D,
mouse-look interactions, footstep-cadence panning artefacts).
