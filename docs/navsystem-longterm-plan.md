# Navigation System — Long-term Design Plan

> **Document status:** Draft / in active design.
> This is a **design plan**, not an investigation and not an implementation guide.
> Engine capabilities and data sources live in `docs/navsystems-investigation.md` — refer there for what the engine *can* do. This file captures what we *will* do with it.
>
> **Working mode:** decisions are walked through one at a time, captured here as we go.
> Implementation strategy (ordering, milestones, kdev/patch scaffolding) is decided **at the end** of the design session, not interleaved with feature design.

---

## North Star Goal

Enable a **fully blind user** to navigate every area of KotOR 1 with:

- **As little extra work as possible** — minimal extra keypresses, no out-of-band note-taking
- **Fluid and efficient** — answers in seconds, not multi-step menu walks
- **Minimal spoilers** — the system never reveals content the player has not yet earned (story, map fog-of-war, hidden items, unidentified NPCs)
- **Minimal atmospheric loss** — the original soundtrack, VO, and ambience remain the dominant audio channel; nav cues live underneath them

The system must work **across the full game** — not just a single area type — so the design needs to generalise across town hubs, dungeon corridors, open exterior maps, the Ebon Hawk interior, the galaxy map, and combat encounters.

---

## Design Principles (derived from goal)

- **Default-on, override-everywhere.** Strong opinionated defaults so a new player can simply start; deep per-feature toggles for users who want to tune.
- **Layered, not monolithic.** Different scales of navigation are separate subsystems with their own activation, mute, verbosity, and audio budget.
- **Engine-respecting.** Read from the same sources the engine renders from (walkmesh, fog-of-war, map-hider) so spoiler-gating, discovery, and quest state come for free.
- **Audio-first, speech-frugal.** Continuous spatial cues for ambient orientation; speech only for on-demand or state-change events.
- **Personal pattern memory.** Where the engine permits, allow the user to save their own markers and routes so the system grows with them.
- **No drive-by mutation.** The nav system reads engine state and produces audio; it does not move the player or change game state without explicit user action.

---

## Design philosophy — the autowalk-vs-exploration spectrum

A standing tension in any blind-accessible nav system, surfaced and addressed early so it shapes the rest of the design.

### The tension

- **Pure cycle-and-autowalk** (filter objects → pick from list → engine drives the character to it) makes the game playable with minimal effort. Many accessibility-focused games use this model. Risk: turns the experience into "pick from list, watch things happen" — erodes agency, exploration, and the sense of *being somewhere*.
- **Pure free-walking** (player walks themselves, navigates by audio cues) preserves agency and atmosphere. Risk: too much work, too cane-tap-stumble, players burn out before finishing the game.

### Honest reality of sighted play in KotOR

Most sighted players spend most of their time doing essentially **scan-for-highlights → click → watch character walk**. The engine's own passive-selection white outline does the scanning work for them. KotOR's level design assumes this. So a cycle-and-autowalk-led blind experience isn't a *degradation* from "real" sighted play — it's a faithful translation of what median sighted play actually looks like.

Where sighted play diverges is in the **moments players choose to slow down**: wandering an interesting environment, standing in a room just to be there, peripheral vision passively building sense-of-place. For KotOR specifically (Manaan stations, Star Forge, Sith bases), that passive sense-of-place matters.

### Design principle: support both modes well, let the player slide

We are **not picking one model**. We design both for genuinely good experience and let the player slide along the spectrum moment-to-moment without switching modes:

- **Pillar 1** provides the background sense-of-place passively, no player action required — change-driven cues for walls / hazards / presence as the player walks.
- **Pillar 4 cycle + cross-cutting autowalk** provides the efficient scan-and-click mode for getting things done.
- **Pillar 2 view mode** provides the explicit "stop and look around" mode for slowing down.
- **Pillar 2 / 3 announcements** provide low-cost ambient orientation context throughout.

The slider is continuous: at any moment the player can be free-walking, autowalking, view-mode-inspecting, or cycle-browsing. They cohabit the same audio language.

### Where the philosophy lives in the design

This shapes the **user-options / personalization layer** rather than architecture:

- Defaults and presets encode where on the spectrum a given user starts (e.g. "first-time-blind-player" → autowalk-leaning; "atmosphere priority" → free-walking-leaning).
- Architecture remains neutral — both modes equally first-class.
- No forced choice; user options express preference, not mode-switching.

### One concrete commitment

**Free walking must be genuinely good, not just barely tolerable.** If users default to autowalk only because free walking is unpleasant, the choice between modes is forced, not real. The Pillar 1 quality bar is therefore non-negotiable — change-driven cues, character-anchored 3D audio, footstep-suppression-when-stuck, and the eventual community-tuned cue palette must add up to *enjoyable* free movement, not just *survivable* free movement.

---

## Architecture Overview — 4-pillar nav system + user options

The user's base design is a four-pillar system:

1. **Small-scale navigation** — *(scope to be defined)*
2. **Medium-scale navigation** — *(scope to be defined)*
3. **Large-scale navigation** — *(scope to be defined)*
4. **Object navigation + filter system** — *(scope to be defined)*

…plus a cross-cutting fifth concern:

5. **User options & personalization layer** — per-pillar toggles, verbosity dials, audio mixing, custom markers, presets

The four pillars are **independent**: the user can disable any one of them entirely without breaking the others. They share a common substrate (engine reads, audio bus, name resolution) but their UX, activation model, and audio profile are distinct.

---

## Pillar 1 — Small-scale navigation

### Scope (locked 2026-05-03)

**"Right around me" awareness.** Continuous spatial sense of the player's immediate surroundings, sufficient to avoid hazards and bumps while moving:

- Walls and static obstacles
- Drop-offs / pits / non-walkable edges (don't fall in)
- Presence of nearby NPCs and interactable objects (presence only — naming/identification is Pillar 4's job)

**Intent:** keep the player physically safe and unstuck. *Not* a decision-making aid — that's Pillar 2's job. The player should be able to move smoothly without hurting themselves or getting trapped on geometry, even before they form a mental map of the room.

### Mechanics (drafted 2026-05-03)

Two complementary sub-features:

**A. Change-driven dynamic directional cues** — for walls, obstacles, hazards, and possibly other kinds.

- **Trigger model:** sounds fire only when the player's spatial relationship to a feature *changes*. Walking along a wall at constant distance = **silence**. Two complementary trigger types coexist:

  **Trigger 1 — Distance-delta (360°, all in-range features).** A cue plays when:
  - A wall / obstacle / hazard enters the awareness range (turning a corner, crossing a threshold)
  - Distance to a tracked feature changes by more than `delta_threshold` (approaching, walking past the end of a wall)
  - A feature exits the awareness range

  **Trigger 2 — Foremost-in-front delta (locked 2026-05-03, narrow ±15° forward cone).** A single cue plays when *which feature is foremost in the player's narrow front cone* changes. This captures the "what is now in my path" signal that pure distance-delta misses on rotation:
  - A new feature becomes foremost (rotation brings something into your path; or you walk past one corner and another wall comes into front)
  - Front cone becomes clear (no in-range feature in the cone — possibly a distinct "clear" cue, possibly just silence)
  - Cone width: **±15° around character heading** (narrow, reads as "path channel" rather than sighted FOV)
  - At most one cue per rotation milestone, regardless of total feature count → no rotation spam

  **Trigger 2 is additive, not restrictive.** Trigger 1's 360° awareness is preserved in full; Trigger 2 layers a focused "what's directly ahead" annotation on top.

- **Sampling model:** **per-engine-tick polling with per-feature delta threshold** (locked 2026-05-03). Both triggers sample at tick rate.
  - Each tick: for every tracked feature in awareness range, compute current distance from player.
  - If `|current_distance − last_cued_distance| > delta_threshold`, fire a cue and update `last_cued_distance` for that feature.
  - This single-state-variable approach naturally handles all change cases:
    - Constant-distance wall-following → delta = 0 → silence
    - Slow divergence (non-90° wall, opening corridor) → delta accumulates → cue when threshold crossed → reference updates → silence until next crossing
    - Continuous approach to obstacle → delta accumulates per tick → cues fire at rhythm `(threshold ÷ walk_speed)` — distance-milestone cadence emerges for free
    - Standing still → no movement, no delta, no cues
  - **Tick rate:** Aurora convention is ~30 Hz for AI/script (per investigation references to per-tick movement updates). Exact rate is an implementation detail; confirm at first hook.
  - **`delta_threshold` value:** calibration knob, not design parameter. Tuned in live testing. Likely user-overridable.
- **Why change-driven (not continuous):** atmosphere preservation. Most of the time the player walks in silence; the system is invisible until it has something useful to say. Acknowledged divergence from the common "constant wall pulse" pattern (e.g. classic Polaris-style); we may add a continuous-mode option later for users who prefer that pattern.
- **Volume (distance attenuation):** automatic. We pass world position to `CExoSound::Play3DOneShotSound`; Miles handles falloff against the listener. Optional clamping via `SetDistance(min, max)`. Falloff curve shape (linear vs exponential between min/max) is tune-by-ear at first hook test, not a design decision.
- **Direction (pan):** automatic, same call. The engine derives pan from source-position-relative-to-listener. We do not implement spatial math.
- **Pitch — initial implementation OMITS pitch encoding** (locked 2026-05-03). Test volume-only first; volume already encodes distance via 3D attenuation. If solo testing shows volume alone is sufficient, pitch never needs implementation. If solo testing shows distance is hard to read from volume alone, add pitch using the locked direction below.
- **Pitch direction (locked for future implementation if needed):** **close = low pitch, far = high pitch** (spatial-mass semantics — close solid mass = deep frequency; distant openness = airy frequency). Decided by user preference; community input no longer needed.
- **Listener pose: overridden to character body every tick** (locked 2026-05-03). KotOR's default is camera-anchored — but the camera sits ~3–5m behind the character and can rotate independently of character facing, which would produce misleading distances and pans. We call `CExoSound::SetListenerPosition(character_pos)` and `SetListenerOrientation(character_forward, world_up)` per tick to anchor 3D audio to the character's body. Two consequences:
  - Cues report distances and directions relative to **where the player thinks they are** (the character).
  - **Camera rotation is invisible to Pillar 1.** Player can move the camera freely without affecting cues.
- **Awareness model: 360° around the character with a uniform range cap** (locked 2026-05-03). No sight cone, no front-bias. Reasoning:
  - A sighted player has a forward cone because eyes face forward — biological constraint we don't share.
  - Blind players don't naturally rotate the camera; forcing rotation to "discover behind" breaks fluid navigation.
  - The change-driven sampler + 3D pan naturally tracks features moving around 360° (e.g. "comes into range left, passes left, fades behind").
  - **Throttle is range, not angle.** Default range conservative (e.g. 5m, user-tunable). Most distant clutter falls out of range automatically; 360° doesn't mean noisy.
  - Optional later refinement: rear-attenuation (back-half audible at reduced range) — additive, doesn't change architecture.
- **Pitch mapping:** pitch encodes *current* distance to the feature — **direction undecided**, pending community input from blind players.
  - Option A: low = close, high = far (spatial-mass semantics — close solid = deep, distant open = airy)
  - Option B: low = far, high = close (alarm semantics — close = urgent = higher frequency)
  - Note: since volume already encodes distance via 3D attenuation, pitch encoding distance is **redundant by design** (defense-in-depth — keeps the distance-readout legible even when ambient game audio drowns out the volume signal). Alternative: free pitch up for a different dimension (kind / urgency / wall material). Decision deferred.
- **Per-kind identity:** each kind of feature (wall, ledge/pit, door, placeable obstacle, NPC) has its own audio identity so the player knows what they just sensed. Pitch dimension is reserved for distance; identity comes from timbre / WAV choice. Vocabulary TBD — see open questions.

**B. NPC / item presence awareness** — for "is there someone or something near me".

- Two implementation paths under consideration, **decision deferred**:
  - **Engine's passive-selection / hover system** (`CClientExoAppInternal::DoPassiveSelection`, `SetLastClickedOnTarget` — investigation §Q6). Reuses the engine's existing logic for "what is the camera framing right now". Pro: aligns with engine semantics; the camera-frame-aware behaviour is built-in. Con: tied to camera, which a blind player isn't actively steering.
  - **Custom presence cues** in the same change-driven framework as (A) — NPCs and items get their own dynamic directional sound as they enter / leave awareness range.
- These aren't mutually exclusive; could combine.

### Cross-pillar boundary note

The change-driven dynamic-directional mechanism could plausibly serve Pillar 2 (medium-scale) as well — sensing the *shape* of a room comes from sensing where its walls are. The user has explicitly parked the mechanism in Pillar 1 for now; revisit when designing Pillar 2 to decide whether to share the engine or have separate ones.

### Engineering basis (all data confirmed in investigation)

- Wall edges from walkmesh adjacency `-1` sentinel — cached once per area-enter (§Q2)
- Non-walkable / hazard surface classification via `surfacemat.2da` (§Q2)
- Per-area object iteration with type discrimination (DOOR / PLACEABLE / CREATURE / WAYPOINT etc.) (§Q5)
- 3D positional one-shots via Miles, engine-managed listener pose (§Q8)
- Player position + facing readable each tick from `CSWSObject.position +0x90` / `orientation +0x9c` (§Q1)

The novel work is the **per-tick "nearest feature in each direction-sector + delta detection"** layer. Everything underneath is engine-supplied.

### Open design questions

- ~~Pitch direction~~ — **resolved 2026-05-03**: close=low, far=high (spatial-mass semantics). Locked per user preference.
- ~~Does pitch need to encode distance at all?~~ — **resolved 2026-05-03**: initial implementation omits pitch entirely; volume-only test first. Add pitch later only if solo testing shows volume alone is insufficient.
- **`delta_threshold` value** — what magnitude of distance change is "worth a cue". Calibrate in live testing.
- ~~Rotation-while-stationary behaviour~~ — **resolved**: handled by Trigger 2 (foremost-in-front delta, ±15° cone). Rotation that brings a new feature into the front cone fires one cue. Pure silence-on-rotation rejected — would lose the "space opens up in front of me" use case.
- **Awareness range / cap value** — default starting point likely ~5m, user-tunable. Per-kind override (e.g. hazards audible further than placeables)? Tune in live testing.
- ~~Direction resolution~~ — **resolved**: continuous via engine 3D audio with character-anchored listener.
- **What features become "obstacles"?** Walls confirmed. Non-walkable areas (pits / ledges / lava) confirmed. Placeable colliders (chests, debris)? Doors as obstacles vs doors as interactables? Open.
- **Audio vocabulary inventory** — how many distinct sounds (one per feature kind) and what are they? Authoring effort scales with this.
- **Approach behaviour** — walking *toward* an obstacle: one cue on entry then silence until distance changes again, or repeated cues at distance milestones (every metre crossed)?
- **Interaction with Pillar 4 and other pillars** — when Pillar 4 is browsing or Pillar 2 is announcing room shape, do Pillar 1 cues keep firing or duck?
- **Combat / cutscene behaviour** — full system, reduced, off?
- **NPC presence: hover-system vs custom-cue** — pick one or combine.

### Mechanics — stuck-detection via footstep suppression (drafted 2026-05-03)

**Design intent:** when the player has movement intent (key pressed) but the character's actual position is not changing (walking into a wall, blocked by collision), **suppress the footstep audio**. No new cue needed — the silence itself signals "you're stuck", consistent with Pillar 1's "silence is the default" principle.

**Pre-existing engine behaviour to override:** KotOR (typical Aurora) plays the walk animation and footstep audio based on movement *intent*, not on actual displacement. Walking into a wall plays the same audio as walking freely — misleading for blind play.

**Implementation approach:**

- **Hook audio path, not animation path** — narrow intercept: only the footstep sound is conditionally suppressed. Visual animation continues unaffected. Far safer than hooking animation events.
- **One-tick lag handles the timing problem** — use last-tick stuck state to suppress this-tick audio. ~33 ms latency at 30 Hz, imperceptible.
  - Per tick: if input had movement intent AND `|position − last_position| < ε` (≈1 cm), set `was_stuck = true` for next tick.
  - Per tick (audio): if `was_stuck`, suppress footstep playback this tick.
- **Filter to player character only** — must not suppress NPC footsteps. Whatever function we hook needs source-object identity check.

**RE work needed (follow-up, not blocking design):**

- Identify the specific function that plays footstep sounds (investigation §Q8 maps the audio stack but not the footstep trigger). Likely an animation-event callback that calls `CExoSound::Play3DOneShotSound` with a `surfacemat.2da` Sound-column resref.
- Confirm we can filter to player character (probably trivial — source object is in the call signature).

**Side benefit:** after this change, footsteps become a **reliable "I am actually progressing" feedback signal**. Currently they aren't (play regardless of movement). Post-change: footsteps = "crossing ground"; silence-with-intent = "stuck". Zero-cost ambient nav cue using engine's existing audio.

**Fallback if RE shows the implementation is too fragile:**

- Fall back to **collision cue reused for character-blocked context** (currently view-mode-only). Same semantic event ("tried to move through something solid"), no new WAV needed. Vocabulary stays at 12.

### Decisions captured

- 2026-05-03 — Pillar 1 covers walls, obstacles, hazards (pits/ledges), and NPC/item presence.
- 2026-05-03 — **Stuck-detection via footstep suppression** (no new cue): when player has movement intent but zero displacement, suppress footstep audio. Silence-when-stuck consistent with Pillar 1 model. RE work to identify footstep trigger function flagged as follow-up. Fallback: reuse collision cue for character-blocked context.
- 2026-05-03 — Wall/obstacle/hazard cues are **change-driven**, not continuous (atmosphere-preserving). Continuous mode parked as a possible later option.
- 2026-05-03 — Cue **volume + pan** are engine-managed via `Play3DOneShotSound` from a world-space source position; we do not implement spatial math.
- 2026-05-03 — Cue **pitch direction deferred** pending community input from blind players. Whether pitch encodes distance at all is also open (volume already does via 3D attenuation).
- 2026-05-03 — **Sampling = per-engine-tick** with per-feature delta threshold. Cue cadence emerges from sampling × threshold × player speed; distance-milestone behaviour is implicit.
- 2026-05-03 — **Listener pose overridden to character body each tick** (not the engine default of camera). All cues are character-anchored; camera rotation invisible to Pillar 1.
- 2026-05-03 — **Awareness model = 360° around character, range-throttled** (no sight cone). Default range conservative (~5m, user-tunable). Optional rear-attenuation parked as a later refinement.
- 2026-05-03 — **Rotation while stationary** — handled by Trigger 2 (foremost-in-front delta, ±15° cone). One cue per rotation milestone, no spam, captures "space opens before me" use case while preserving 360° awareness via Trigger 1.
- 2026-05-03 — **Two trigger types:** Trigger 1 = distance-delta (360°, all in-range features); Trigger 2 = foremost-in-front delta (±15° narrow cone, additive). Trigger 2 is a path-channel signal, not a sight cone — Trigger 1's 360° awareness is preserved.
- 2026-05-03 — Per-feature-kind **distinct audio identity** (timbre / WAV varies; pitch axis reserved for distance).

---

## Pillar 2 — Medium-scale navigation

### Scope (locked 2026-05-03)

**"In this space" orientation — the area a sighted player can see at once.**

Starting calibration: roughly the camera-frame range of a sighted player (KotOR's third-person camera shows ≈ a room or a corridor segment at a time). Iterate smaller if testing shows the cone is too noisy at full sight range.

The pillar enables **spatial decision-making**, not just survival:

- "I want to explore this room left-half first, then right-half, then take the corridor at the end" — the user can plan a route without inch-by-inch scanning
- "Where are the exits / doors / openings out of this room" — find structural transitions cheaply
- "What's the rough shape of this space" — corridor vs room vs open plaza

**Intent:** give the player the same room-scale spatial picture a sighted player gets at a glance, so they can make informed movement choices.

### Mechanics — view mode (drafted 2026-05-03)

A "look around the room without moving the character" mode that reproduces the sighted-player experience of pausing, mousing the cursor around, and inspecting objects.

**Distinct from existing modes:**
- Pillar 4 cycle = discrete list-of-objects step-through
- Pillar 3 map-cursor = 2D scan on the map UI
- View mode = continuous 3D scan in normal gameplay view, from the player's standpoint, without budging the character

**Activation:**
- Toggle key (suggested **V**, TBD finalize)
- On entry: virtual cursor placed at character world position
- Exit: V again, OR any character-movement key (auto-exit when player starts actually walking)

**Cursor movement:**
- Same key scheme as map-cursor (bound movement actions: forward / back / strafe / turn keys all map to XY-plane movement)
- Cursor moves in world XY plane at character's Z height
- Continuous motion at configurable speed (same fluidity model as map-cursor)

**Wall collision:**
- Per-tick walkmesh check on cursor target. If movement would cross a wall edge (data already cached from Pillar 1), block and emit a **collision cue** at the wall point.
- Cursor's reachable region ≈ current room + open doorways into adjacent rooms — matches sighted line-of-sight from the character's standpoint.
- Spoiler-safe by construction: can't peek into undiscovered rooms because cursor can't pass through walls.

**Per-tick info while cursor moves:**
- Tooltip / examine-box info for the object under cursor — read via TTS using engine's existing tooltip + `ShowExamineBox` data sources.
- Room transitions announced (same logic as sub-feature A, but cursor-driven instead of character-driven).
- TTS rate-limit: probably "speak on hover-pause" rather than every tick to avoid spam (TBD calibration).

**Continuous object findability sounds (view-mode only):**
- Small ambient loops on key objects (interactables, NPCs, doors, containers) within cursor's reachable area.
- Played via owned `CExoSoundSource` voices anchored to each object's world position; engine pans + attenuates per cursor / character listener.
- **Justification for the divergence from Pillar 1's "no constant audio" rule:** view mode is *explicit active scanning*. The user opted in. Constant ambient is appropriate when the user is actively looking; not appropriate when they're just walking.
- Stops immediately on view-mode exit; normal change-driven Pillar 1 resumes.

**Click-to-walk:**
- A hotkey (TBD — could be same V to "walk and exit", or a separate dedicated key) hands cursor's world position to the cross-cutting guidance service (auto-walk + audio beacon).
- Effectively reproduces the sighted "click somewhere on screen to walk there" interaction.

**Pillar 1 interaction:**
- Pillar 1 wall tones don't need explicit suspension — character is stationary in view mode, no distance deltas, no cues fire naturally.
- Collision cue is a *cursor-triggered*, view-mode-only cue, separate from Pillar 1's character-anchored cues.

### Open sub-questions for view mode

- Cursor speed / fluidity values (likely same as map-cursor; tunable in user options)
- ~~Which objects get continuous loops~~ — **resolved direction:** reuse engine's curation. Filter = objects in passive-selection range AND with tooltip/cursor-type set. No new "is this important" classifier needed. Final tuning live.
- Click-to-walk key choice (V again, or separate key)
- Per-cursor-tick TTS strategy (hover-pause-debounced vs continuous)

### Engine curation sources we reuse (don't reinvent)

The engine already maintains multiple "what's important" classifications, each suited to a different pillar:

- **Passive selection** (`DoPassiveSelection`) — objects near the player worth highlighting. Used for view-mode continuous loops.
- **Cursor-type on hover** (`CSWSTrigger.cursor`) — per-interaction-type enum (door / pickup / unlock / talk / etc.). Used for kind-aware Pillar 4 announcements without us classifying anything.
- **Map-note presence** (`CSWSWaypoint.has_map_note`) — engine's own "important landmark" curation. Used for elevated Pillar 4 + Pillar 3 cues.
- **Tooltip presence** — engine's "worth a label" curation. Filter for view-mode loops.
- **`GAME_OBJECT_TYPES` enum** — engine's structural type classification. Used for Pillar 4 categories.

Design rule: **reuse engine curation, don't invent our own "is this important" rules** unless the engine genuinely lacks the classification we need (e.g. Pillar 1 obstacle/hazard kinds — we define those explicitly because the engine doesn't classify obstacles for accessibility purposes).

### Mechanics — context announcements (drafted 2026-05-03)

Four sub-features, all reading from existing engine state:

**A. Room-transition announcement.** Per-tick, call `GetRoom(area, &player_pos, NULL)` and compare to last tick's room. On change, announce the new room's localized name (`CSWSArea.room_names[i]`). E.g. "Crew Corridor".

**B. Area-transition announcement (cross-module).** When the player crosses a trigger that loads a new module:
- Hook `CClientExoApp::AddMoveToModuleMovie @0x5edb50` to announce "loading: going to Manaan Docks" *before* the loading movie starts.
- On area-enter, announce the new `CSWSArea.name +0x150`.
- Two-stage: loading → arrived.

**C. Octagonal direction-on-turn announcement.** Read `CSWSObject.orientation +0x9c`, convert to bearing (`atan2(y, x) * 180/π`), quantize to 8 compass sectors (N, NE, E, SE, S, SW, W, NW). On sector change, announce the new direction. **Default ON** (per user — known polarising feature, but the user prefers it on; users who dislike it disable in options). User-options toggle to disable.

**D. On-demand orientation/zone hotkey.** A free hotkey (specific binding TBD) with two modes:
- **Plain press** — current facing in degrees, no quantization (e.g. "47°"). For fine-grained orientation when needed.
- **Shift+press** — current zone hierarchy, closest level to biggest. KotOR's hierarchy is typically 3 levels: room → area/module → planet. Sources:
  - Room: `GetRoom` → `room_names[i]`
  - Area/module: `CSWSArea.name +0x150` and `CSWSModule.name_localized +0x64`
  - Planet: `CSWGuiInGameGalaxyMap.current_planet +0x254c` (or a small module→planet fallback table)
- Example output: "Crew Quarters, Upper City North, Taris".

### Engineering basis (all confirmed in investigation)

- Per-area room array + names: `CSWSArea.rooms +0x230`, `room_names +0x25c` (§Q5)
- Point-in-room query: `GetRoom @0x4bb600` (§Q2)
- Area name: `CSWSArea.name +0x150` (§Q5)
- Module name: `CSWSModule.name_localized +0x64`, `area_name +0x30` (§Q7)
- Loading-movie name accessor: `CClientExoApp::AddMoveToModuleMovie @0x5edb50` (§Findings #8)
- Player orientation read: `CSWSObject.orientation +0x9c`; bearing math from `ExecuteCommandGetFacing @0x537fe0` (§Q1)
- Planet name from galaxy map: `CSWGuiInGameGalaxyMap.current_planet +0x254c` (§Q4)

### Open design questions

- **Hotkey choice for sub-feature D** — needs an unused key. `,` `.` `Shift+,` `Shift+.` `-` are taken by Pillar 4. TBD.
- **Octagonal sector boundary hysteresis** — to avoid stuttering when player oscillates around a sector boundary, may need a small dead-zone. Tune live.
- **Bearing frame** — world-frame or map-frame? Pillar 3 map-cursor mode uses world coords; for orientation announcements, world-frame is simpler. Map-frame (rotated for the area's narrative-north) is an option for later.
- **Loading-screen announcement timing** — before or after the screen displays? Probably before, so user knows what's loading. Confirm at hook time.
- **Combat / cutscene behaviour** — same as Pillar 1: full system, reduced, or off?

### Decisions captured

- 2026-05-03 — Pillar 2 includes: room-transition announcement, area-transition announcement (load + arrive), optional octagonal direction-on-turn (default OFF), on-demand hotkey for degrees / zone-hierarchy.
- 2026-05-03 — Zone hierarchy is typically 3 levels (room → area → planet) in KotOR; not always 4. Announcement adapts to what's available.
- 2026-05-03 — Auto octagonal direction-on-turn defaults **ON** (user preference; disable available in user options for users who dislike it).
- 2026-05-03 — **View mode** added to Pillar 2: toggle key (V), virtual cursor starts at character, walkmesh-bounded with collision cue, optional continuous object findability sounds, click-to-walk via guidance service. Reproduces sighted "stop and look around" experience. Distinct from Pillar 4 cycle (discrete list) and Pillar 3 map-cursor (2D map).
- 2026-05-03 — View mode is the **only context where continuous ambient cues are appropriate** — user opted in via mode toggle. Normal play remains change-driven per Pillar 1's locked model.

### Calibration anchor

KotOR world units are floats; engine convention is **1.0 unit = 1 metre** (per investigation §Q1). Need a one-time runtime check via `personal_space` on a humanoid creature (≈ 0.5–1.0) before locking radii.

---

## Pillar 3 — Large-scale navigation

### Scope (locked 2026-05-03)

**"How do I get there" routing at long-distance scale.** Star-map and town/planet-map level navigation — the kind of overview a sighted player gets from the in-game galaxy map or the full-screen area map of a hub planet (Taris upper city, Manaan docks, etc.).

Covers:

- Long routes within a large hub map (multi-area town navigation)
- Cross-area transitions (which exit goes where)
- Planet-to-planet travel (galaxy map)

**Intent:** the player can plan and execute long journeys without getting lost between intermediate landmarks. Player retains full agency — what target to pick, which landmarks to visit / avoid, preferred path.

### Mechanics — guidance to a selected target (drafted 2026-05-03)

Once the player has chosen a target, two **independently-toggleable** modes guide them there. Both can be on, both off, or one of each — not mutually exclusive.

**Mode A — Auto-walk.**

- Issues `CSWSCreature::AddMoveToPointAction(player, ..., destination, ...)` (or the inter-area equivalent for cross-module routes).
- Engine's existing pathfinder + AI move-to loop drives the character. We do not implement movement.
- Player input interrupts auto-walk per engine convention (need to verify exact behaviour, presumably: any directional input cancels).
- Cross-area transitions handled by engine via `AIActionCheckInterAreaPathfinding` / `SetMoveToModulePending` chain.

**Mode B — Audio beacon at next turning point.**

- The pathfinder produces a waypoint sequence (engine's `path_points` / `path_connections` graph nodes selected for this query).
- A **beacon** is set at the **nearest unreached turning point** on the path.
- Beacon emits cues using the **Pillar 1 audio mechanics**, fully shared:
  - Same per-tick sampling, same change-driven trigger model, same character-anchored 3D positional audio, same engine-managed pan + volume.
  - Beacon is just one more feature kind in the Pillar 1 vocabulary — its own distinct sound (so the player can tell beacon-cues apart from wall/obstacle cues).
- When player reaches the beacon's coordinates (within a tolerance — exact value TBD), a **"reached" cue** plays and the beacon advances to the next waypoint in the path.
- When the final waypoint is reached, a **"destination reached" cue** plays and the beacon stops.

The two modes compose cleanly: with both on, auto-walk moves the player while the beacon provides situational awareness of approaching waypoints. With Mode B alone, the player walks themselves and the beacon serves as a guide.

### Architectural note — guidance is cross-cutting

Although the user introduced this in Pillar 3 (large-scale), the guidance mechanism (target selection → auto-walk + audio beacon) is **useful at every scale**:

- **Pillar 3 selection:** pick a planet on the galaxy map, an area entrance, a far map note → guide there.
- **Medium-scale (Pillar 2):** pick "the nearest unexplored exit from this room" → guide there.
- **Pillar 4 (object cycle):** cycle to a focused object, press a hotkey → "guide me to this thing". This closes the loop on the future pathfinding feature parked in Pillar 4 earlier; it's not a separate thing, it's Pillar 4 handing a target to the shared guidance service.

**Architecture decision (proposed, pending confirmation):** target selection lives in each pillar (different UIs at different scales), but the guidance service (auto-walk + audio beacon, including the beacon's reuse of Pillar 1 mechanics) is **a shared cross-cutting subsystem** that any pillar can invoke with a `Vector` destination.

### Engineering basis

- `CSWSCreature::AddMoveToPointAction` — engine move-to API; full 17-arg signature decoded (investigation §Q3) — the auto-walk path
- `CSWSArea.path_points +0x23c` / `path_connections +0x244` — explicit nav graph nodes/edges (the pool turning points come from)
- Cross-area: `AIActionCheckInterAreaPathfinding`, `SetMoveToModulePending`, `SetMoveToModuleStartWaypoint` chain (investigation §Q3) — handles trigger-crossings and module loads
- Per-creature path query state on `CSWSCreature.path_find_info +0x340`

### Open RE item

The **computed path solution** (the specific waypoint sequence the solver picked for *this* query, not just the global graph) lives on `path_find_info` per investigation §Q3 but the exact field offset for the "current path waypoint list" is not yet decoded. Two paths forward, both viable:

1. RE the field at hook-development time, read the engine's solution directly.
2. Re-solve A* ourselves over the `path_points` / `path_connections` graph (cheap — graph is already in memory). Trade-off: parameters might diverge from engine's path slightly.

Flagged so it's not forgotten. Does not block design.

### Pathfind trigger key (locked 2026-05-03)

**`Shift+-`** initiates pathfind to the Pillar 4 currently-focused object. Output behaviour depends on the user's persistent mode toggles for the cross-cutting guidance service:

| Autowalk | Beacon | Output |
|---|---|---|
| ON | ON | Both start: engine drives character; audio beacon plays at next waypoint |
| ON | OFF | Autowalk only |
| OFF | ON | Beacon only (player walks themselves, beacon guides) |
| OFF | OFF | Path description readout (TTS) — fallback so the keypress is never silent |

(Note: the table above is for design-doc readability only — runtime announcements never use markdown tables, per CLAUDE.md.)

**Cancellation:** `Shift+-` pressed again while a pathfind is active → cancels (auto-walk, beacon, or description-in-progress). Toggle behaviour, no separate cancel key needed.

### Path description granularity (locked 2026-05-03)

The path description's usefulness depends on its level of abstraction. The engine's pathfinder produces per-tick low-level waypoints (around tables, between chairs, through doorways) — narrating these verbatim is noise.

Three plausible levels:

- **Brief** — total distance + destination + transition count. "Path: 47 metres to cantina door, crossing 1 area transition." Cheap, useful as fallback.
- **Medium** — segment-grouped. "12 metres east to crew corridor, 8 metres north to cantina door, through to cantina." Requires post-processing: group consecutive waypoints by direction (~±20°), identify room-boundary crossings, drop obstacle-avoidance jiggles. Non-trivial but tractable.
- **Verbose** — every raw waypoint. Useless. **Never ship.**

**Initial scope:** Brief as the fallback when autowalk + beacon both off.

**Future enhancement:** Medium with a path-abstraction layer; once available, expose an option to always-speak the Medium summary alongside other guidance modes (the "planning overview" use case).

**Possible engineering shortcut:** the engine's `CSWSArea.path_points` + `path_connections` (per investigation §Q3) is a per-area nav graph that may already be at meaningful-navigation-hub granularity rather than per-step. If so, narrating the graph nodes the path crosses *is* the Medium summary at no extra cost. RE confirmation needed; if true, Medium becomes near-free.

### Future / optional ideas (labelled — may not be worth it)

**Multi-area route-choice prompt.** When a long-distance path crosses ≥2 area transitions and meaningfully different alternative routes exist, prompt the player: "via Upper City or via the Sith Base?". Implementation:

- K-shortest-paths (Yen's algorithm) over the area-level graph (areas as nodes, transitions as edges) — small search space, tractable.
- Prune to alternatives that (a) pass through *different* areas, and (b) have cost within ~25% of optimal. Otherwise auto-pick optimal silently.
- Modal multi-choice prompt UI built on the same accessibility patterns as dialog menus.

**Caveat (KotOR-specific):** the game's geography is mostly linear; meaningful route choice between distant areas is rare in practice. The feature might trigger only a handful of times per playthrough. Engineering complexity = moderate (~2-3 weeks); value-per-effort = uncertain.

**Status:** future / optional. Defer the decision until the basic guidance service has shipped and we can see how often this would actually fire in real play.

### Open design questions

- **Target selection mechanism.** Where/how does the user pick a target? Per-context UIs (galaxy-map cycle, area-map cycle, world Pillar 4 cycle) all hand a target to the same guidance service?
- **Spoiler gating on destinations.** Only let the player target discovered locations (`IsWorldPointExplored`, `CSWGuiMapHider`)? Allow known-but-unvisited as targets?
- **Quest-objective integration.** "Set current quest objective as nav target" as a one-key shortcut?
- **Off-path behaviour (Mode B alone).** If the player walks off the computed path, do we re-solve from current position, stay locked to original waypoints, or warn?
- **Cancellation.** How does the player cancel an active beacon / auto-walk? A dedicated key, or repurpose Pillar 4's `-`?
- **Cross-area transitions in Mode B.** When a waypoint is on the other side of a trigger (door / area transition), does the beacon fire a special "transition coming up" cue? Auto-walk mode handles it engine-side, but Mode B needs to surface it explicitly.
- **Reached-tolerance value.** How close must the player be to a waypoint before "reached" fires? Tune live.
- **Beacon range / visibility.** Is the beacon always audible (Pillar 1 awareness range doesn't apply to it), or also range-throttled?

### Map system clarification

KotOR has **one area-map UI with two display sizes**, plus a separate galaxy map:

- **Corner minimap** — small overlay during gameplay
- **Fullscreen area map** — same data, expanded view (toggle via map key)
- **Galaxy map** — separate UI for planet selection

Corner-minimap and fullscreen-map are the **same `CSWGuiInGameMap` data** at different render sizes — not separate systems. Throughout this doc, "map" / "minimap" / "area map" all refer to this one entity.

The map shows **curated landmarks**: map notes (named waypoints), player position, fog-of-war room outlines, area transitions. **Not every container or random NPC** — the map is intentionally summary. The camera view is intentionally comprehensive. Two different sighted-information channels with different detail levels; our design translates each separately (Pillar 4 cycle scope handles this distinction explicitly).

### Mechanics — map-cursor exploration (drafted 2026-05-03)

Distinct from Pillar 4's marker-cycle on the same UI. Two complementary interaction patterns on the map:

- **Pillar 4 cycle** (`,` / `.` / `Shift+,` / `Shift+.` / `-`) — step through discrete known markers / categories. "What landmarks exist on this map."
- **Map-cursor explore** (proposed: in-world movement keys while map UI active) — continuous spatial scan. "Tell me about *this part* of the map."

Mechanics:

- A virtual cursor lives in map pixel space; translated to world space via the inverse of `GetMapPixelFromWorldCoord` (4 lines, see investigation §Q4).
- Cursor moves via the player's **bound in-world movement actions** when the map UI is the active panel — not hardcoded keys. We read the actions (forward / back / strafe-left / strafe-right / turn-left / turn-right) so user remaps follow:
  - **Forward** → cursor north
  - **Back** → cursor south
  - **Strafe-left** AND **turn-left** → cursor west
  - **Strafe-right** AND **turn-right** → cursor east
  - Both pairs bound on the X axis: turn-keys give muscle-memory horizontal movement; strafe-keys give pure-axis movement. Either works.
- **Fluidity model:** continuous motion while key held (per-tick step at configurable speed), with Pillar 4 cycle as the "jump-to-marker" complement on the same UI for fast hops. Optional Shift-modifier for fast-sweep speed parked as a possible later refinement.
- Cursor step in *pixel* space → world step in metres scales naturally with zoom.
- Per-tick, while cursor moved:
  - If cursor world-coord changed room (`GetRoom` returns a different `CSWSRoom`), announce the new room's name (`room_names[i]`).
  - If cursor is over a map marker / waypoint with map note (per `HitCheckMouse` or our own iteration of `map_pins[]` filtered by `IsWorldPointExplored`), announce its `localized_name` / `map_note`.
  - If cursor is over an unexplored region (fog-of-war set), announce "unexplored" or stay silent (configurable).
- Spoiler model: never expose names of unexplored markers; respect `CSWGuiMapHider`'s discovery filter where the engine already provides it.
- Zoom-aware: cursor in pixel space → zoom changes world-units-per-pixel automatically. Granular movement when zoomed in, broad sweep when zoomed out.

Caveat — naming-granularity reality: KotOR hub cities are typically **multiple modules/areas** stitched by transitions, not one map with named "quarters". So "entering north quarter" maps to either:

- Engine-side: a transition trigger fires when crossing the area boundary (already an event we can hook).
- Within-map cursor: room-level granularity (room_names) — generally building-interior rooms, not cardinal-direction sub-regions.

Both are supported, via different mechanisms.

### Decisions captured

- 2026-05-03 — Pillar 3 includes pathfinding + guidance to a player-selected target.
- 2026-05-03 — Two independent guidance modes: auto-walk (engine AI move-to) and audio beacon (at next path waypoint).
- 2026-05-03 — Audio beacon **reuses Pillar 1 mechanics in full** — same sampling, change-driven cues, character-anchored 3D audio. Beacon is its own feature kind in the Pillar 1 vocabulary with a distinct sound.
- 2026-05-03 — "Reached" cue advances beacon to next waypoint; final waypoint produces a "destination reached" cue.
- 2026-05-03 — Guidance mechanism is **cross-cutting** (proposed): target selection per-pillar, guidance service shared. Pillar 4's parked pathfinding feature is one client of the shared service, not its own thing.
- 2026-05-03 — **Map-cursor explore mode** added to Pillar 3: virtual cursor on map driven by in-world movement keys, announces room transitions / markers under cursor / unexplored regions. Complementary to Pillar 4 marker cycle on the same UI; distinct keybindings.
- 2026-05-03 — **Pathfind trigger = Shift+-**, output depends on user's autowalk + beacon mode toggles. Press again to cancel (toggle behaviour). Description-only fallback when both modes off.
- 2026-05-03 — **Path description granularity:** initial scope is Brief (total distance + destination + transition count) as fallback only. Medium (segment-grouped, "12m east to corridor") deferred until path-abstraction layer is built; once available, can be option to always-speak alongside other guidance modes. Verbose (every raw waypoint) never ships.
- 2026-05-03 — Cursor key bindings follow **the player's bound movement actions** (forward / back / turn / strafe), not hardcoded WASD/QE — user keymaps remain the source of truth.
- 2026-05-03 — Fluidity: **continuous motion at configurable speed** while key held; Pillar 4 cycle is the marker-jump complement.
- 2026-05-03 — **Quest objectives = normal map markers**, no special category. A "set current quest objective as nav target" shortcut hands that marker to the cross-cutting guidance service. Open engineering question: how to identify "the current quest objective" marker (likely from journal state — TBD at hook time).
- 2026-05-03 — **Planet picker UI = menu accessibility task**, not nav-system design. Inherits the existing in-game-menu accessibility approach (per the work already shipped on dialog / inventory panels). Out of scope for this plan.
- 2026-05-03 — **Pillar 3 batch lock**: position+facing on map = reuse Pillar 2 Shift+orientation hotkey when map UI active; distance-to-destination = milestone-based (≈ 200/100/50/20/5 m); saved named user markers = yes (nice-extra) via `CSWGlobalVariableTable.locations`; spoiler-gated destinations (only target discovered locations); "set current quest objective as nav target" shortcut on its own bound key; off-path beacon behaviour = silent re-solve from current position; beacon always audible (ignores Pillar 1 awareness range); cross-area transition cue fires when beacon's next waypoint is on the other side of a transition (uses Transition/Exit cue from vocabulary).
- 2026-05-03 — **Pillar 2 batch closeouts**: bearing frame = world-frame for now (map-frame parked as future option); orientation/zone-hierarchy hotkey *slot* reserved (specific key chosen at implementation); octagonal-direction sector hysteresis ≈ 5° dead-zone (calibration).
- 2026-05-03 — **Pillar 4 batch lock**: sort by distance-from-player ascending; direction frame = clock-position relative to player facing; empty categories silently skipped on `Shift+,`/`Shift+.`; spoiler gating per engine fog-of-war + curation (no parallel filter built).
- 2026-05-03 — **View mode batch lock**: cursor speed shares user-options entry with map-cursor; click-to-walk on its own key (e.g. Enter) while view mode is active; TTS rate uses hover-pause debounce (~300 ms quiet → speak), not continuous.
- 2026-05-03 — **Pillar 1 closeouts**: all 8 per-kind sounds in the vocabulary participate in the change-driven cue system (walls, hazards, doors, NPCs, placeables, items, landmarks, transitions); NPC presence cue is our own (character-anchored), engine `DoPassiveSelection` may serve as a curation filter for "which objects are noteworthy enough".
- 2026-05-03 — **Cross-pillar locks**: spoiler model = read from engine state (fog-of-war, map-hider, passive-selection, quest state) → spoiler-correctness emerges from data source choice. Combat = nav system inherits with reduced Pillar 1 verbosity (user-options toggle). Cutscenes = nav cues mostly OFF (only Pillar 2 transition announcements may still fire if triggered mid-cutscene).

---

## Pillar 4 — Object navigation + filter system

### Architectural note (2026-05-03)

This pillar is best understood as a **cross-cutting UI primitive** rather than a peer of the three scale pillars. Same keybindings, same mental model, surface different data sets depending on what the player is currently doing:

- During normal gameplay → world objects in the current area (engine `GAME_OBJECT_TYPES`)
- With the area map open → map markers / waypoints
- With the galaxy map open → planets / known destinations

It is kept as its own numbered pillar in this document for design clarity, but in implementation it will likely be one shared cycle/announce engine fed by per-context data providers.

### Keybindings (locked 2026-05-03)

Three keys, single global scheme that works across all surfacing contexts (gameplay / area map / galaxy map):

- `,` — cycle to **previous item** within the current category
- `.` — cycle to **next item** within the current category
- `Shift+,` — cycle to **previous category**
- `Shift+.` — cycle to **next category**
- `-` — **announce** the currently focused item (name + direction + distance)

Implementation note: hook by virtual-key code with shift modifier; layout-agnostic (works the same on QWERTZ and QWERTY).

### Per-item announcement (locked 2026-05-03)

When `-` is pressed, the system announces:

- **Name** — resolved via the standard chain (CExoLocString inline → TLK strref → tag fallback; per investigation §Q7)
- **Direction** — relative to player position/facing (frame TBD: clock-position vs cardinal)
- **Distance** — in metres

Engine world units are floats with the convention 1.0 unit = 1 metre (investigation §Q1, STRONG). Validate at first hook by reading `personal_space` on a humanoid (expect ≈ 0.5–1.0).

### Categories (locked 2026-05-03 — six)

Curated from the engine's `GAME_OBJECT_TYPES` enum (per investigation §Q5):

1. **Door** — DOOR objects. Sub-states: locked / unlocked / open / cross-area transition.
2. **NPC / Creature** — CREATURE objects. Sub-states: party / friendly / hostile / dead.
3. **Container / Placeable** — PLACEABLE objects with `usable=true` OR `has_inventory=true`. Interactable scenery + lootables.
4. **Item** — ITEM objects (ground items). Distinct from containers (pick up vs open).
5. **Landmark** — WAYPOINT objects with `has_map_note=true`. Engine's own "important location" curation.
6. **Transition / Exit** — TRIGGER objects with `transition_destination` set. Cross-area exits.

**Curated out:**

- PROJECTILE, AREAOFEFFECT, SOUND, AREA — not player-facing
- ENCOUNTER — script-only, invisible
- STORE — folded into Placeable (rare; stores are placeables that open trade UI)

**Sub-states are TTS modifiers, not separate categories** (initial scope). E.g. "Locked door, north, 3 metres" — same door cue + spoken state. If user testing shows people want sub-states encoded sonically, add variant WAVs later.

For map / galaxy-map contexts, the same six categories apply where the game data fits; design our own only if testing shows the engine's classification is insufficient.

### Future feature — pathfinding to focused item

Once an item is focused, the user may want a route. Two design options on the table:

- Combine with `-` (announce-then-path)
- Separate hotkey (announce stays cheap; pathfind opt-in)

Decision deferred — needs the basic cycle UX shipped and tested first.

### Open design questions

- ~~Cycle scope~~ — **resolved 2026-05-03**: in normal gameplay, **current room + line-of-sight extension** (objects visible through open doors / openings count as "in scope"). Translates the camera-view channel of sighted play. On map UI, scope shifts to map-curated landmarks across the whole area (translates the map channel). Different scopes for different contexts; same keybindings.
  - **LOS engineering note:** engine has `CSWRoomSurfaceMesh.los_material_mask +0xd8` for which walkmesh materials are line-of-sight transparent. Implementation likely walks adjacency edges from current room and includes adjacent rooms whose connecting edges are LOS-transparent (cheap), rather than per-object raycasts (expensive). Refresh when player crosses room boundary.
- **Sort order within category** — by distance from player, by direction (e.g. clockwise sweep), or stable by object id?
- **Empty categories** — when `Shift+.` lands on a category with zero items, skip to the next non-empty, or land and announce "0 doors"?
- **Direction frame** — clock position relative to player facing ("door at 10 o'clock"), or compass relative to map-north ("door to NW")? Player-relative is likely more useful for actually moving toward it.
- **Category curation** — expose the full GAME_OBJECT_TYPES enum or pre-filter to player-relevant types?
- **Spoiler gating** — does the cycle skip undiscovered objects (per `IsWorldPointExplored` / `CSWGuiMapHider`), or expose them and let the user decide?
- **Pathfinding trigger** — `-` combined, or separate hotkey?
- **Reset / clear focus** — is there a way to drop focus, or does it always have a current item once you've cycled in?

### Decisions captured

- 2026-05-03 — keybindings: `,`/`.` cycle items, `Shift+,`/`Shift+.` cycle categories, `-` announces.
- 2026-05-03 — announcement payload: name + direction + distance (metres).
- 2026-05-03 — categories sourced from engine `GAME_OBJECT_TYPES` (curation TBD).
- 2026-05-03 — same key scheme reused across gameplay / area map / galaxy map.
- 2026-05-03 — **Six categories locked**: Door, NPC, Container/Placeable, Item, Landmark (waypoint+map_note), Transition/Exit. Curated from engine `GAME_OBJECT_TYPES`. Sub-states (locked, hostile, etc.) handled by TTS modifiers, not separate categories.
- 2026-05-03 — **Cycle scope** = **current room + line-of-sight extension** in normal gameplay (translates camera channel); curated landmarks across whole area when map UI is open (translates map channel). Same keybindings, different data per context.

---

## Cross-cutting — User options & personalization

### Initial scope (locked 2026-05-03)

For first implementation, **ship with one fixed set of defaults** — no preset system, no per-feature toggle UI yet. Presets remain a future enhancement (the philosophy section above sketched the structure).

**Persistence: global** (per game install, applies to all saves). No per-save profile, no save-editing involvement. Custom markers also global.

### Locked defaults

**Pillar 1 — small-scale**
- All 8 per-kind sounds: ON (walls, hazards, doors, NPCs, placeables, items, landmarks, transitions)
- Trigger 1 (distance-delta, 360°): ON
- Trigger 2 (foremost-in-front, ±15° cone): ON
- Footstep suppression on stuck: ON
- Awareness range: **5m** (starting; tune live)
- Distance-delta threshold: **0.5m** (starting; tune live)
- Voice budget: **3 simultaneous max** (starting; tune live)

**Pillar 2 — medium-scale**
- Room transition announcement: ON
- Area transition announcement: ON (loading + arrived two-stage)
- Octagonal compass-on-turn: ON
- Octagonal sector hysteresis: **5°** (starting; tune live)
- Bearing frame: world-frame
- View mode: key bound (V suggested), inactive until pressed
- View mode continuous object findability loops: ON when view mode is active
- View mode TTS hover-pause: **300ms** (starting; tune live)

**Pillar 3 — large-scale**
- Audio beacon: ON
- Autowalk: ON (both fire on Shift+- pathfind invocation)
- Pathfind: triggered only on Shift+- keypress; never automatic
- Path description fallback: automatic when both autowalk and beacon are off (won't fire normally given both on)
- Map cursor explore mode: ON when map UI active
- Player position + facing on map: enabled (on Shift+orientation hotkey while map active)
- Distance-to-destination milestones: **200 / 100 / 50 / 20 / 5m** (starting; tune live)
- Reached-tolerance (beacon waypoint): **1m** (starting; tune live)
- Saved named user markers feature: enabled, empty preset list
- Multi-area route-choice prompt: OFF (future feature)

**Pillar 4 — object cycle**
- Cycle keybindings active (`,` / `.` / `Shift+,` / `Shift+.` / `-`)
- All 6 categories ON
- Cycle scope: current room + LOS extension (gameplay) / area-curated landmarks (map UI)
- Sort order: distance from player ascending
- Direction frame: clock-position relative to player facing
- Empty-category skipping: silent skip on `Shift+,`/`Shift+.`
- Spoiler gating: ON (engine fog-of-war + curation)
- Pathfind trigger Shift+-: enabled

**Cross-pillar**
- Movement key swap (A/D = strafe, Q/E = turn): shipped via engine keybind config
- Combat verbosity reduction: ON
- Cutscene nav cues: mostly OFF (only Pillar 2 transition announcements may fire if triggered mid-cutscene)
- Audio mixing: nav cues use SoundEffect slider category (engine-managed); priority groups conservative (will calibrate live)

### Future enhancements (deferred)

- **Preset system** — "first-time-blind-player" / "atmosphere-priority" / "sighted-experience-translation" structure sketched in the design philosophy section. Build once the fixed-defaults baseline is shipped and we have user feedback on the spectrum.
- **Per-feature toggles UI** — exposes individual on/off + tuning sliders for users who want deep control.
- **Pitch direction** — deferred for community input from blind players.
- **Medium path-description granularity** — requires path-abstraction layer.
- **Sub-state distinct WAVs** — initial ships TTS modifiers; sonic sub-states added if testing shows demand.

---

## Cross-pillar — Movement model (locked 2026-05-03)

Affects every pillar. Decided after evaluating grid-step vs free-form vs hybrid.

**Decision: stay with the engine's natural free-form continuous movement, with A/D ↔ Q/E key swap so default movement is non-rotating.**

### Why not grid

Considered grid-step (player presses direction → engine auto-walks one grid unit). Rejected because:

- Pressing a direction key implying "character now faces that direction" creates an **audio-frame discontinuity**: a wall on the player's "left" suddenly becomes "in front" because the character turned. Cognitive load disproportionate to the simplicity gain.
- Lately-blinded users — likely a large share of our audience — bring sighted-gaming muscle memory. Engine-natural WASD movement preserves that.
- Open exteriors, ramps, narrow precision sequences fit the engine's continuous model better than any discrete grid step.

### Why the A/D ↔ Q/E swap

KotOR's default movement bindings:

- W / S — forward / back
- **A / D — turn left / right** (rotates character)
- **Q / E — strafe left / right** (sidesteps without rotating)

Default A/D rotation is the source of the audio-frame instability problem: any sideways movement intent ends up rotating the character, so spatial cues shift their reference frame mid-movement.

After the swap:

- W / S — forward / back (unchanged)
- **A / D — strafe** (sidestep, **no rotation**)
- **Q / E — turn** (explicit, deliberate rotation)

Default movement (W/A/S/D) never rotates the character. The audio frame stays stable. Trigger 2 (foremost-in-front delta) only fires when the player explicitly chose to rotate via Q/E, which is exactly the semantic we want — a deliberate "look around" action.

### Implementation note (locked 2026-05-03)

**Use the engine's existing keybind system. No hooks on player movement.**

- Our patch ships **swapped defaults** for the move actions (A/D bound to strafe, Q/E bound to turn) via KotOR's normal keybind config.
- KotOR's keybind UI — already part of the menu-accessibility work — lets the user rebind however they want, exactly like a sighted player would.
- We do **not** hook the input layer for player movement. Engine handles WASD-equivalent movement at full native fidelity.
- Map cursor (Pillar 3) still requires a hook because it's not an engine feature; that's a separate, narrow surface.
- Camera control hooks — deferred until we know whether we need them for any feature.

This aligns with the user's standing preference for driving existing tooling rather than reimplementing.

### Personalization

Defaults swapped (the swap *is* the default in our patch). Anyone who wants KotOR-original bindings remaps in the engine's standard keybind UI. No special toggle required — the engine's own settings page already does this job.

### Implications for the four pillars

- **Pillar 1** — audio frame is stable under default movement; Trigger 2 cues fire only on intentional rotation or genuine path-geometry changes. The whole rotation-cognitive-load worry from earlier discussions resolves naturally.
- **Pillar 2 / 3 / 4** — unchanged in design; just inherit a more predictable audio-frame from the swap.
- **Map-cursor mode in Pillar 3** — already designed to follow the player's bound movement actions. With the swap, A/D-strafe naturally becomes the X-axis cursor mover; works as previously specified.

---

## Cross-pillar — Audio vocabulary inventory (locked 2026-05-03)

Shared across Pillar 1, Pillar 3 beacon, Pillar 4, and view mode. **Single WAV per kind serves multiple pillars** — one-shot for Pillar 1 change-cues + Pillar 4 cycle, looped softly for view-mode findability.

### Per-kind cues (8)

Six map to the locked Pillar 4 categories; two are Pillar 1 geometry features without an object-type equivalent:

1. **Door**
2. **NPC / Creature**
3. **Container / Placeable**
4. **Item**
5. **Landmark** (named waypoint with map note)
6. **Transition / Exit**
7. **Wall** (Pillar 1 — geometry feature, walkmesh perimeter edge)
8. **Hazard / Ledge** (Pillar 1 — non-walkable walkmesh face)

### Special-purpose cues (4)

9. **Collision** (view-mode cursor hits wall)
10. **Beacon active** (Pillar 3 audio beacon at next path waypoint)
11. **Beacon waypoint reached** (advance to next waypoint)
12. **Beacon destination reached** (final waypoint of guidance)

### Total: 12 WAVs (initial scope)

Authoring constraints:
- Mono, 22 kHz or 44.1 kHz PCM, ~200 ms each (longer for view-mode loops if a separate variant turns out to be needed)
- Each kind gets a distinct timbre / instrument so the player can tell them apart on first hearing
- Pitch axis reserved for distance encoding (per Pillar 1 — pending community input on direction)
- Drop in `<install>/Override/` per investigation §Q8; engine resolves CResRef via standard search chain

### Sub-state handling

Sub-states (locked door, hostile NPC, opened container) handled by TTS modifiers in announcement text, not by additional WAVs. If user testing shows sonic sub-state encoding is wanted, expand the inventory then.

### Future / optional additions

- Pitch / timbre variants for sub-states
- Continuous-mode wall pulse variant (alternative to change-driven, for users who prefer that pattern)
- Per-material walking-surface footstep enhancement (already supplied by engine via `surfacemat.2da`; not new authoring needed)

---

## Cross-pillar — Spoiler model (locked 2026-05-03)

**Principle: blind player gets exactly the information a sighted player has — no more, no less.**

Implementation strategy: **read from the same engine state sighted players see.** Spoiler-correctness emerges from data source choice rather than from custom filtering rules.

Concrete data sources we read from:

- **`CSWSAreaMap.IsWorldPointExplored(Vector)`** — fog-of-war per grid cell. If a sighted player hasn't seen it, we don't reveal it.
- **`CSWGuiMapHider`** — engine's map-note discovery filter. Pins / waypoints hidden until earned remain hidden in our cycle/cursor too.
- **`DoPassiveSelection` curation** — engine's "objects close enough to highlight to the sighted player". If the engine considers an object too far / occluded to highlight, we treat it as not-yet-noticed.
- **Journal / quest state** — only quest objectives the sighted player has unlocked are exposed as "set current objective" targets.
- **Per-area zone names** (`CSWSArea.name`, `room_names`, galaxy-map planet labels) — these *are* what the sighted player reads on the map / world. We expose them identically.

Most of this is automatic. As long as we sit on top of these engine surfaces rather than building parallel data structures, spoiler-correctness is a property of the design rather than a feature we have to enforce.

---

## Cross-pillar — Combat / cutscene behaviour (locked 2026-05-03)

**Cutscenes:** most pillars OFF. Player isn't moving; nav cues add no value. Pillar 2 context announcements (zone transitions) may still fire if they're triggered mid-cutscene; otherwise silent.

**Combat:** nav system inherits naturally.

- Pillar 1 change-driven cues still work
- Pillar 4 cycle still works (enemies are CREATURE category; cycle to them, hear distance/direction)
- Cross-cutting guidance still works

**Reduce Pillar 1 verbosity during combat** by default — combat audio is dense; routine wall cues become noise. Suggested combat-mode reduction: keep hazards, collision, and foremost-in-front Trigger 2; suppress routine wall distance-delta cues. User-options toggle "combat nav verbosity" exposes this.

**Combat-specific accessibility** (action queues, HP state, who-is-attacking-whom announcements, etc.) is **out of scope for this nav plan**. Separate future module.

---

## Cross-pillar concerns (parking lot — fill in as they surface)

These are themes that touch multiple pillars and need a shared answer:

- **Spoiler model** — what does "the player has earned this information" mean operationally? Is it `IsWorldPointExplored`, quest-state flags, NPC met-before, dialog node visited?
- **Audio mixing budget** — how many simultaneous nav voices, what priority group, how do they duck against game audio and against each other?
- **Speech vs cues** — when does the system speak (TTS via Tolk) vs play a non-verbal cue?
- **Activation model** — is the system always-on, always-on with hotkey-mute, or hotkey-on-demand? Probably mixed per pillar.
- **Combat behaviour** — full system, reduced system, or fully off during combat rounds?
- **Cutscene / dialog behaviour** — fully off, or only critical state-change announcements?
- **Persistence** — what state survives save/load (custom markers? last-announced object? options profile?).

---

## Engine foundation — already-investigated capabilities

Detailed in `docs/navsystems-investigation.md`. Quick map of which engine surfaces feed which pillar:

- **Player position + facing** (`CSWSObject.position +0x90`, `orientation +0x9c`) → all pillars
- **Walkmesh + wall edges** (`CSWRoomSurfaceMesh` adjacency `-1` sentinel) → Pillar 1 (wall audio, floor type)
- **Per-area object lists** (`CSWSArea.game_objects`, typed by `GAME_OBJECT_TYPES`) → Pillar 4 (cycle/filter), Pillar 2 (in-area objectives)
- **Map-pin / map-note system** (`CSWGuiMapHider::GetNextMapNote`, fog-of-war) → Pillar 2 (in-area orientation, spoiler-respecting)
- **Galaxy map + planets** (`CSWGuiInGameGalaxyMap`) → Pillar 3
- **Pathfinder entry points** (`CSWSCreature::AddMoveToPointAction`) → potential "move to selected target" action across pillars
- **Name resolution** (CExoLocString → TLK strref → tag fallback) → all pillars
- **3D audio via Miles** (`CExoSound::Play3DOneShotSound`, owned `CExoSoundSource` voices, SFX volume slider) → Pillar 1 spatial cues; speech still goes through Tolk
- **NWScript-equivalent reuse paths** (`ExecuteCommandGetPosition`, `…Distance…`, etc.) → safe side-effect-free reads from hooks

There is **nothing in the goal that the engine cannot already supply.** The work is design, plumbing, audio authoring, and UX — not reverse engineering.

---

## Implementation strategy

### Patch architecture (locked 2026-05-03)

**Single `.kpatch` file, multi-file C++ source organized by subsystem (not by pillar).**

Considered: multiple `.kpatch` per pillar (too much DLL boundary friction for shared state — listener pose, audio bus, name resolver, area cache, guidance service all cross pillars); single monolithic `.cpp` (unmaintainable past ~3000 lines); single `.kpatch` with pillar-organized folders (looks tidy but pillars share so much code that cross-pillar `#include`s creep in immediately — wrong abstraction at code level).

**Pillars remain a design / UX concept** but disappear from code organization. The design doc keeps its pillar structure (right user-experience framing); code is organized by what it does.

### Code structure (proposed)

```
patches/Accessibility/
├── manifest.toml
├── hooks.toml
├── exports.def
├── src/
│   ├── core/                  # plumbing — high-trust stable layer
│   │   ├── hooks.cpp            # central hook installer
│   │   ├── logging.cpp
│   │   ├── settings.cpp         # user options I/O
│   │   ├── tolk_bridge.cpp      # screen reader output
│   │   └── init.cpp             # subsystem wiring / startup
│   ├── engine/                # engine state readers (pure reads)
│   │   ├── player_state.cpp     # position, facing, area, room
│   │   ├── area_cache.cpp       # walkmesh edges, object lists, room lookups
│   │   ├── name_resolver.cpp    # CExoLocString → TLK chain (SEH-guarded)
│   │   └── input_state.cpp      # bound action queries
│   ├── audio/                 # all audio output
│   │   ├── audio_bus.cpp        # CExoSound wrappers
│   │   ├── listener_override.cpp # per-tick character anchor
│   │   ├── cue_player.cpp       # 12-WAV vocabulary playback
│   │   └── footstep_suppress.cpp # stuck-detection footstep gating
│   ├── spatial/               # change-driven spatial awareness
│   │   ├── change_detector.cpp  # per-tick distance-delta sampling (Trigger 1)
│   │   ├── front_cone.cpp       # foremost-in-front (Trigger 2, ±15°)
│   │   └── awareness_range.cpp  # range-throttled feature query
│   ├── announce/              # TTS announcements
│   │   ├── transitions.cpp      # room + area transition announcements
│   │   ├── compass.cpp          # octagonal direction-on-turn
│   │   ├── orientation.cpp      # zone-hierarchy hotkey
│   │   └── degrees.cpp          # on-demand degrees readout
│   ├── filter/                # object categorization + cycle state
│   │   ├── categories.cpp       # the 6 categories + curation
│   │   ├── cycle_state.cpp      # current focus / category / sort
│   │   └── spoiler_gate.cpp     # discovery filter via engine state
│   ├── input/                 # keyboard input handling
│   │   ├── cycle_keys.cpp       # ,/./Shift+,/Shift+./- bindings
│   │   └── hotkeys.cpp          # other bound hotkeys
│   ├── guidance/              # cross-cutting nav guidance
│   │   ├── autowalk.cpp         # AddMoveToPointAction wrapper
│   │   ├── beacon.cpp           # audio beacon at next waypoint
│   │   ├── pathfind.cpp         # path query + waypoint extraction
│   │   └── description.cpp      # Brief path description readout
│   ├── view_mode/             # the "stop and look around" mode
│   │   ├── cursor.cpp           # virtual cursor + walkmesh collision
│   │   ├── continuous_loops.cpp # findability ambient loops
│   │   └── click_walk.cpp       # cursor-to-walk handoff
│   ├── map_ui/                # map UI augmentation
│   │   ├── cursor.cpp           # map-cursor explore mode
│   │   └── markers.cpp          # saved user markers + map iteration
│   └── menus/                 # existing menu accessibility (refactored)
│       └── ... (existing Accessibility.cpp split here)
```

### Layering rules

- **`core/` and `engine/` are foundation.** Higher layers depend on them; never the reverse.
- **`audio/`, `spatial/`, `announce/`, `filter/`, `input/`** are siblings — each depends on `core/` + `engine/`, not on each other (some narrow exceptions: `spatial/` calls `audio/cue_player`).
- **`guidance/`, `view_mode/`, `map_ui/`** are higher-level features that compose lower subsystems. They depend down, never sideways into peers.
- **`menus/`** stands alone (existing accessibility surface for in-game menus; doesn't interact with nav-system).

### Pillar → subsystem mapping (traceability for the design doc)

- **Pillar 1** = `spatial/` (change_detector + front_cone) + `audio/cue_player` + `audio/footstep_suppress`
- **Pillar 2** = `announce/` (transitions + compass + orientation + degrees) + `view_mode/`
- **Pillar 3** = `guidance/` + `map_ui/` + `announce/transitions` (on cross-area)
- **Pillar 4** = `filter/` + `input/cycle_keys` + `announce/` (name + direction + distance) + invokes `guidance/` on Shift+-
- **Cross-pillar movement-key swap** = ships in keymap config, not code

### Why subsystem layout > pillar layout for our case

- Cross-cutting code lives in subsystems naturally; no awkward "which pillar owns this" decisions
- Bug location is unambiguous: "wall cue wrong" → `audio/cue_player` + `spatial/change_detector`, period
- Refactoring within a subsystem doesn't ripple
- Standard pattern across the modding ecosystem (SKSE / F4SE / NWN extender plugins)

### Refactoring transition

Existing `Accessibility.cpp` (menu accessibility) refactors **incrementally** rather than upfront:

- New nav-system code lands in the new structure from day one.
- Existing menu code stays where it is; refactors into `menus/` piecewise as we touch it (e.g. when user-options menu work happens).
- Minimizes risk to working features; new structure proves itself before forcing a global rewrite.

### Phasing — single cohesive plan, multiple sessions (locked 2026-05-03)

**One implementation chunk** for Phases 0–6. **Phase 7 deferred indefinitely** (only if needed).

User explicitly chose this over a community-feedback gate between phases — solo play and self-test will validate Pillar 1 design choices before any community release. No public beta milestone in this plan.

The "single chunk" is **planning + implementation cohesion**, not session count. The plan is one body of work; the work spans many sessions with explicit discipline checkpoints between them.

#### Phases (locked order)

- **Phase 0 — Refactor.** Extract `core/` + `engine/` from monolithic `Accessibility.cpp`; split menu code into `menus/`. Verify menu accessibility regression. **Exit criterion:** new structure in place; existing menu features still work end-to-end. Commit.
- **Phase 1 — Foundation.** `audio/audio_bus.cpp` + `audio/listener_override.cpp`; `engine/player_state.cpp` + `engine/area_cache.cpp`; `core/settings.cpp`; author 12-WAV vocabulary into `Override/`. **Exit criterion:** test fixture can play a 3D positional cue at any world position with character-anchored listener. Commit.
- **Phase 2 — Playable baseline.** Pillar 4 cycle (`filter/` + `input/cycle_keys` + `announce/` for name+direction+distance) + cross-cutting `guidance/autowalk` + Pillar 2 `announce/transitions` (room + area). **Exit criterion:** game is playable end-to-end via cycle-and-autowalk. Solo playthrough of an area works. Commit.
- **Phase 3 — Pillar 1.** `spatial/change_detector` + `spatial/front_cone` + `audio/cue_player` + `audio/footstep_suppress` (with RE work for footstep function; fall back to collision-cue reuse if RE proves fragile). **Initial implementation omits pitch**; volume-only test first. **Exit criterion:** free walking is genuinely informative; solo test confirms wall/hazard/object cues fire correctly without spam. Commit.
- **Phase 4 — Pillar 2 polish + view mode.** `announce/compass` + `announce/orientation` + `announce/degrees` + `view_mode/` (cursor + continuous loops + click-walk). **Exit criterion:** rotation announcements work; view mode lets player inspect rooms without moving character. Commit.
- **Phase 5 — Pillar 3 polish.** `guidance/beacon` + `guidance/pathfind` + `guidance/description` + `map_ui/cursor`. **Exit criterion:** Shift+- triggers pathfind with audio beacon and autowalk; map cursor explores fullscreen map. Commit.
- **Phase 6 — Map markers & nice extras.** `map_ui/markers` (saved user markers); planet picker accessibility integration; multi-area route choice prompt remains future. **Exit criterion:** named markers persist across saves; full feature set shipped. Commit + release.
- **Phase 7 — User options UI.** Deferred. Build only if needed. Hardcoded defaults from the User Options section may be sufficient indefinitely.

#### Implementation discipline (locked 2026-05-03)

These rules apply across all phases. They protect against the real failure modes (context-window degradation, scope creep, debugging chaos):

- **Hard checkpoint at every phase exit.** Commit. Do not start the next phase in the same session that completed the current one. Fresh session for fresh phase.
- **Within a phase, single-topic sessions.** A session implements one subsystem or one bug fix at a time. No "while we're here let's also fix X" — note it, defer it, fresh session.
- **One bug at a time during testing.** If three things look broken, debug one, ship the fix or revert, then look at the next. Three concurrent debug threads in one session is how regressions stack.
- **No unscoped feature additions during phase work.** If a feature seems useful but isn't in the locked plan, add to a "future ideas" list, never silently bolt on. If genuinely critical, stop the phase, update the plan explicitly, then continue.
- **Lay-off points within long phases.** For phases that span >1 session (likely Phase 2, 3, 5), define internal hand-off points: e.g. "session N ends when filter/categories.cpp compiles cleanly and is unit-tested; session N+1 begins with cycle_state.cpp". Each hand-off = commit + fresh session.
- **Refactor scope is fixed at Phase 0.** Subsequent phases do not refactor existing code unless explicitly necessary for the new feature. "Drive-by improvements" go to a future-cleanup list.
- **RE work bounded.** When RE for a hook or function takes longer than half a session, stop and reassess. Most surfaces we need are mapped in the investigation; if something genuinely isn't, fall back to documented alternatives (e.g. footstep RE → collision cue reuse) rather than open-ended decompilation.

---

## Decision log

Each design decision, once locked, gets one line here with a date and a pointer to the pillar section that holds the detail.

*(empty)*
