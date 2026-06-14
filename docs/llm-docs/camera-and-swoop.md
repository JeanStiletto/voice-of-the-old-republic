# Camera & swoop-pad internals (RE reference)

Camera screen-edge turn mechanism, A/D-vs-W/S decoupling, mouse-look gating, and swoop accelerator-pad classification.

> Migrated from the agent memory store on 2026-06-14 (memory-system cleanup).
> Each section below is one former memory note, preserved verbatim. Verify
> addresses/offsets against current code before relying.

## camera_spin_edge_turn
_Endless camera spin = engine screen-edge turn (cursor in 1px edge band) feeding AcclTurnCamera accumulator; plus camera_orient quaternion-read bug_


The "character spins endlessly, no input, touchpad fixes it" bug. Decompiled 2026-06-11.

**Root cause = engine screen-edge camera turn.** `CClientExoAppInternal::UpdateCamera` @0x5f5e10 runs every frame and turns the camera horizontally from one of three sources:
- 0x11c turn axis (`PollInput(ExoInput,0x11c)` clamped [-1,1]) → `AcclTurnCamera`. Our synthesized A/D (camera_orient) feeds this; a lost key-up (focus theft) leaves it stuck → spin.
- **cursor in left/right edge band** → `AcclTurnCamera(dir)`. Band = `max(1, viewport_width * screenFramePercentage)`. `screenFramePercentage` @0x7a2444 default = **0.001** → band ≈ **1px**. Fires when `GuiManager->mouse_x <= 1` or `>= width-1` (OS cursor clamped at monitor edge). Left→+1, right→-1, continuous.
- else mouse dx → `TurnCamera`.

Gate: edge-turn fires when `uVar8==0`. `uVar8 = rightClickHeld`, inverted if `ClientOptions.field_0x8 & 2` (= **mouse_look** bit, CClientOptions +0x8 bitfield: auto_level:1, mouse_look:1, ...). With Mouse Look=0 (user's setting) edge-turn is active by default with no button. Enabling mouse_look would require right-click-held — but changes the whole scheme, NOT a safe fix.

`CSWCModule::AcclTurnCamera` @0x640090 / `TurnCamera` @0x63fe10: in the default follow-cam branch (field1_0xc not 3/5/7, combat_mode==0) they **accumulate** the increment into follow-behavior `+0x40` each frame → unbounded spin. (camera_mode==5 branch sets player orientation directly via Yaw/YawPitchRoll/SetOrientation.)

**CONFIRMED in-game 2026-06-11** (patch-20260611-165637.log): deliberate cursor-to-left-edge reproduced it. 393 consecutive `CameraSpinDiag` lines `mouseX=0/800 edge=LEFT suspect=EDGE-CURSOR weDrive=0 rightClick=0 mouseLook=0`, quatYaw sweeping full revolutions at steady ~180-190°/s. Root cause = engine screen-edge turn, definitively. Fix #1 validated: `diff=0.0` on every line (quatYaw==posYaw) across full range. `cachedYaw` field (module+0x98) was a wrong guess (reads 0/-1, not the accumulator) — drop it.

**SHIPPED 2026-06-12 (v0.4.5, committed, "hopefully fixed"):**
- Fix #1 (commit 5e264c5): `acc::engine::GetCameraYawRadians` (engine_player) — correct w,x,y,z read, `fwd.x=2(xy-zw)`, `fwd.y=1-2(xx+zz)`, `atan2(fwd.y,fwd.x)`, East=0, == position-derived. camera_orient fallback + probe_camera_state use it; deleted bogus 2*atan2(qz,qw). NOT the spin cause, just a latent read bug.
- Guard = `camera_spin_diag.cpp` (commits b479bbf + c103614). The fix: when edge-turn detected (cursor in band + camera rotating ≥3°/s + foreground), **write GuiManager->mouse_x directly to viewport centre** (vpW/2). Catches the contact before AcclTurnCamera ramps → no runaway.
- **KEY LESSON — injected motion does NOT move mouse_x after load.** Both `SetCursorPos` AND `SendInput` MOUSEEVENTF_MOVE left mouse_x stuck at 0 (post-load input pipeline dead; same family as [[project_bigpicture_focus_theft_dead_menus]]). Real touchpad works, synthetic doesn't. Direct field write is the only thing that moved it. (probe_mouselook's SendInput feeds GetMouseDelta=turn, NOT mouse_x=position.)
- Diagnostic now **episode-based** (not per-frame): `edge-guard START` / `edge-guard END` summary (corrections/edgeFrames/duration/maxRate/netYaw) per spin episode + rate-limited `READ ANOMALY` tripwire (quat vs pos yaw divergence = fix#1 regression). Kept in to catch recurrence.

**CAUTION reading the log:** big swings at `mouseX=400 edge=none` (not at edge, not weDrive) are LEGIT user turns (A/D / Q/E target-snap), NOT edge-coast — don't misattribute (I did once; user corrected). Only `mouseX=0 EDGE-CURSOR` frames are the bug.

**IF IT RECURS (guaranteed fallback):** the direct write is a once-per-tick poll and can lose the timing race (engine re-asserts mouse_x=0 some frames before UpdateCamera reads it; AcclTurnCamera accumulates a TARGET angle the follow-cam eases toward, so missed frames coast). The robust fix = detour on `UpdateCamera` @0x5f5e10 clamping mouse_x out of the band at point-of-read. Entry has an SEH-frame prologue (`6a ff 64 a1 00 00 00 00 68 78 5d 72 00...`) — DON'T hook entry (broke input pipeline before per InitializeDirectInputMouse note); hook mid-function. Clamp minimally (0→bandPx+1, not centre) to avoid menu cursor disruption. `screenFramePercentage=0` does NOT disable (max(1,..) floors band at 1px).

**Fix #1 (camera_orient yaw read) — confirmed buggy but NOT the spin cause.** Engine `Quaternion` is **w,x,y,z** (w@+0, see [[project_turret_engine_model]] sibling); camera orientation at camera+0x88 (Gob+0x84). `camera_orient.cpp ReadCurrentEngineYawRad` assumes x,y,z,w, reads engine y/z as "qz/qw", computes garbage `2*atan2(y,z)` — that's why the author wrongly called the quaternion "multivalued" and fell back to atan2(player-camera). The position-derived path actually tracked real motion (the log's 255° "jumps" were the camera genuinely edge-spinning). Correct path: engine `Yaw(Quaternion*)` @0x4aa0f0 (also 0x4a9f40), `Pitch`@0x4aa130, `Roll`@0x4acb90, `YawPitchRoll`@0x4acac0, `Quaternion::zdir`@0x43e250. Decompile workflow: [[reference_ghidra_headless_decompile]].

---

## kotor_camera_character_decoupling
_Stock-engine behaviour — A/D pans camera without rotating character; the character "snaps" to camera yaw only when W or S is pressed. Verified blind via AltGr heading announce 2026-05-06._

KOTOR's camera-on-a-stick behaves like a "soft" decoupled rig in stock play:

- **A/D rotates the camera only.** The character does NOT turn while you hold A or D. AltGr's `announce_degrees` (server-side player yaw) confirms heading is unchanged after arbitrary A/D presses.
- **The character "snaps" to camera yaw at the instant W or S is pressed** to commit forward motion. Character then walks in the camera-facing direction.
- This is true regardless of Caps Lock state and regardless of the `CClientOptions.mouse_look` bit (verified 2026-05-06 with Shift+B probe).

**Why:** This is the design implication that made view mode trivially achievable. View mode = freeze the W/S movement clobber so the snap-and-walk can't happen; A/D continues to pan camera natively. No Mouse Look forcing, no SendInput delta synthesis, no Free Look behavior swap needed. See `view_mode.cpp` + lay-off 3 progress entry.

**Caps Lock "Toggle Free Look" appears to do nothing in K1** even though `CSWCameraFreeLook` exists in the binary (struct + ctor at 0x0063a5d0). Either cut, visual-only, or reachable through a chain we haven't located. Not pursued — not needed for view mode.

**How to apply:** When designing camera-side accessibility features, assume A/D = camera, W/S = "commit turn + walk". Don't synthesise mouse motion to pan the camera unless you specifically need finer-grained control than A/D provides; A/D is the engine-native primitive.

---

## mouselook_engine_behaviour
_CClientOptions.mouse_look is the runtime gate; SendInput reaches engine; cursor not captured. Three load-bearing facts for view-mode design._

Three findings from Phase 4 lay-off 2 probe (`patches/Accessibility/probe_mouselook.{h,cpp}`, `engine_options.{h,cpp}`). Verified in-game 2026-05-06 — strong-positive user report of audible spatial-audio pan during synthetic mouse sweep.

**1. `CClientOptions.mouse_look` IS the runtime gate.** Bitfield `int @+0x8`, mask `0x2`. Flipping from our DLL takes effect immediately without menu interaction. Long-term plan's hint at "near +0xb0" was a misread — that offset is `CSWCameraOnAStick.mouseCameraRotateToggle` (a runtime camera struct, not the user-facing setting). Direct memory write is sufficient — no engine handshake needed.

**2. `SendInput` relative-motion deltas reach the engine through Mouse Look ON.** `MOUSEEVENTF_MOVE` events from our DLL produce identical behaviour to a real mouse: camera rotates → camera-anchored listener rotates → spatial audio pans audibly. KOTOR is from 2003 and could have used DirectInput / raw HID polling that bypasses `SendInput`; it doesn't.

**3. KOTOR does NOT capture the cursor in Mouse Look mode.** Unlike modern FPS games, the OS cursor remains free and moves with each `SendInput` delta. Over a +1000px sweep the cursor escapes the game window. Implication for view mode: continuous look-around input needs explicit `SetCursorPos` recentring between emits to keep the cursor anchored.

**Why:** These are the load-bearing engine assumptions behind the view-mode design path. If any of them flipped (e.g. the engine ignored runtime bit-flips, or used raw HID, or captured the cursor), the chosen path "force Mouse Look ON + synthesise mouse motion from look-around keys" would break and view mode would need a virtual-cursor implementation instead.

**How to apply:** When building view-mode lay-offs (Phase 4 lay-off 3+):
- Use `acc::engine::SetMouseLook(true)` on entry, restore prior state on exit. Chain walk + offsets in `engine_options.h`.
- Drive look-around via `SendInput`-injected `MOUSEEVENTF_MOVE` deltas at tick rate. Magnitude per tick is the "mouse sensitivity" knob.
- Recentre cursor (`SetCursorPos` to game-window centre) after each emit so the OS cursor stays anchored. KOTOR's input read takes the delta regardless of cursor position, so recentring doesn't confuse the engine.
- Camera-anchored listener (Phase 1 lay-off 4 finding) gives audio feedback for free — no separate audio-listener wiring needed.

---

## swoop_accelpads_are_enemies
_Where booster pads live in the engine — iterate enemies (vtable[0x1c]=AsEnemy), not obstacles, to expose them as cues_

Swoop racing has TWO separate object pools, and the accelerator (boost) pads
are in the one we don't iterate yet.

**Layout** (verified against `m03mg.are`, `m17mg.are`, `m26mg.are`; counts identical):
- `<list label="Obstacles">` — 22 entries `mgo01..mgo22`. These are the
  rocks/debris. Spawned as `CSWMGObstacle`, accessible via
  `vtable[0x20] = AsObstacle`. **This is what we already iterate.**
- `<list label="Enemies">` — 30 entries each riding a track (`mgt02..mgt31`).
  All use model `mgf_accelpad01`, death sound `mgs_accelpad`, `Trigger=1`.
  These are the accelerator pads. Spawned as `CSWMiniEnemy` (extends
  `CSWTrackFollower`), accessible via `vtable[0x1c] = AsEnemy`.

The `Trigger=1` flag means they don't shoot — they just detect a collision
with the player follower and fire the area-level `OnHitFollower=accelpad`
script (`tar_m03mg_s.rim/accelpad.ncs`), which calls `SWMG_SetPlayerSpeed *
1.05`, `SWMG_SetPlayerAccelerationPerSecond * 1.10`, and plays the "boost"
animation.

**Why:** User asked whether boost pads were (a) different objects we don't
read, (b) mis-classified obstacles, or (c) terrain zones. Answer is (a):
they're a separate engine type entirely. We routed-by-name in
`swoop_race.cpp::CueResrefForObstacle` looking for "acc"/"boost" substrings
in obstacle names — but no obstacle ever carries that name, because
accelpads aren't obstacles. Names like `m03mg_MGO01..MGO22` are sequential
rock IDs.

**How to apply:** To expose booster pads, add a second per-tick sweep in
`TickObstacleCues` (or a sibling `TickAccelpadCues`) that calls
`CallAsCast(obj, 0x1c)` instead of `0x20`, on the same 255-slot MGO array.
Filter by enemy whose model name (or .gob name accessor) starts with
`mgf_accelpad` — there are no non-accelpad enemies in any of the three swoop
tracks, so the filter is mainly future-proofing. Position comes from the
track follower's interpolated position along its `mgt##` track; need to
locate the world-position field on `CSWTrackFollower` (NOT the
`MiniPlayer.offset_vector` at +0x1c4 — that's the lateral tunnel offset, not
absolute world pos). Likely path: read first model handle from
`CSWTrackFollower.models` (CExoArrayList at +0x68) and read its CAurObject
world position the same way we do for obstacles (`+0x78`).

---

