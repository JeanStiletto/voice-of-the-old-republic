# Positional combat speech — how it works

Combat announcements spoken FROM the world position they are about, through
the engine's own 3D sound path. Built and tested 2026-10-27 (KOTOR 1 tested in
game; KOTOR 2 built with decompile-witnessed values, not yet tested).

The engine mechanism underneath (playing a WAV that has no file) is in
`in-memory-sound-resource.md`. This doc is the feature on top of it: what it
does, how the pieces fit, the decisions behind it, and the traps already hit.

---

## What the player gets

- Mod settings → "Positional announcements" (`Positionierte Ansagen`; named
  "Combat announcements" until 2026-10-28) submenu → toggle
  "3D combat announcements" (`PositionalCombatSpeech`, persisted in
  `acc_settings.ini`, default ON), then four sliders (2026-10-28): volume and
  speed of the normal lines, volume and speed of the urgent lines.
- Each spoken combat line sounds from the direction its effect lands, with
  the engine's panning, SFX slider and EAX/dsoal reverb, and the mod's own
  loudness-over-distance curve (see Placement).
- Normal lines imitate the screen reader's voice where possible; urgent lines
  use the announcement (SAPI) voice the user picked.
- Volume is the mod's own per channel (10..100 %, default 100), never the
  screen reader's or the urgent-volume slider's. Speed is an offset from the
  voice's own rate (-50..+50 rate points, step 5, default 0 = "same as screen
  reader" / "same as announcement voice"), so it keeps following NVDA.
  Keys: `PositionalVolumePercent`, `PositionalRateOffset`,
  `PositionalUrgentVolumePercent`, `PositionalUrgentRateOffset`. Each slider
  press plays a sample straight ahead through the same path (`Preview`, on
  priority group 0x0b so it survives the in-game menu pause).
- Off, or whenever a line cannot be placed, the line is spoken exactly as
  before (screen reader / SAPI urgent, centred). Nothing is dropped.

## Decisions (with the user, 2026-10-27)

- **Placement rule: a line sounds from WHERE ITS EFFECT LANDS.**
  - party attack → the TARGET (incl. doors / placeables). The engine already
    plays a swing sound AND a hit sound per attack at the ATTACKER's position
    (`CSWCCreature::PlaySwingSound` @0x0060dc50, `PlayHitSound` @0x00617470,
    both `Play3DOneShotSound` at the attacker +1.5 z), so the target position
    is the new information.
  - incoming hit → the hit party member.
  - status / save / effect / kill / absorb / deflect → the affected creature.
  - "X benutzt Y" (a cast, no target yet) → the caster.
- **Party attack lines name their target again**, short form
  `"<actor>[, <feat>], <target>, <dmg> <type>"` (combat_log.cpp
  `BuildOutgoingLine`). The June 2026 results-only redesign had dropped it.
- **No "real screen reader" option.** A screen reader can never be
  positional: the mod hands it text one way and it plays its own audio.
  SAPI2SR (a SAPI voice that forwards to the screen reader) renders nothing
  either. So the screen-reader option is an IMITATION of its voice.
- **Eloquence only if already installed** (NVDA IBMTTS add-on's 32-bit
  `ECI.DLL`). Never shipped or copied — no distribution rights.
- **No bundled eSpeak NG** ("rarely used; those users can sort it out").
- **Two channels, as today**: normal (screen-reader voice) and urgent
  (announcement voice) — each speaks one line at a time, in order; the two may
  overlap, like NVDA + SAPI did before.

## Pieces

`patches/Accessibility/`:

- **`positional_speech.{h,cpp}`** — the feature. `Speak(channel, text,
  subject, nearName)` / `Tick()`.
  - Finds the subject by NAME in the current area (`FindSubject`): creatures,
    doors and placeables; compares the engine display name
    (`GetObjectDisplayNameByHandle`, the bytes the combat log prints) and, for
    doors/placeables, `GetObjectBaseName`. Same-named objects: the one closest
    to `nearName`'s object (an attack target: closest to its attacker), else
    to the player.
  - Stores the server HANDLE; the position is read when the line STARTS
    playing, so a moving creature is heard where it is then.
  - Renders: Eloquence on a background worker; SAPI / OneCore synchronously
    (~16 ms). Any failure → centred fallback (`SpeakCentred`).
  - Plays via `LoopSource::StartFromMemory` on one source per channel,
    alternating resrefs `acc_tts_n0/n1`, `acc_tts_u0/u1`. Checks
    `IsVoiceAttached()` right after: Play returns a duration even when it
    refused the source (out of range, no free voice), so the voice pointer is
    the only proof.
  - Level chain: peak-normalise to -1 dBFS, then channel volume × distance
    gain as the per-source game volume (127 = full).
  - Placement (`Place`), see the section below.
  - Calls `prism::NoteSpokenElsewhere` when a line starts: the
    `Speech.spoke` log entry, `LastSpoken()` for Ctrl+R, and braille.
- **`combat_log.cpp`** — `SpeakCombatLine(line, subject, nearName)` /
  `SpeakCombatLineUrgent(line, subject)`: every spoken combat line goes
  through these; they try positional first, else the old path. The kill line
  is spoken verbatim; its victim is sliced from the text after `kill_marker`
  up to `:` — best effort per language; a slice naming nothing stays centred.
- **`audio_loop.{h,cpp}`** — `StartFromMemory`, `IsVoiceAttached`,
  `LogPlayState`; the injection (`InjectWave`) and its constants, both games.
- **`audio_wav.{h,cpp}`** — mono 16-bit RIFF builder (channels averaged: the
  engine refuses non-mono 3D), `MeasureLevels`, `NormalizePeak`.
- **`prism.{h,cpp}`** — `RenderUrgentToMemory`, `RenderSapiVoiceToMemory`,
  `RenderOneCoreToMemory` (all on RENDER-ONLY backend instances, see traps),
  `RenderedSpeech`, `NoteSpokenElsewhere`.
- **`eloquence_render.{h,cpp}`** — loads the installed `ECI.DLL`, renders on
  one persistent worker thread; `IsNvdaSynth`, `Submit`, `TakeFinished`, and
  a blocking `RenderToMemory` for the probe.
- **`nvda_config.{h,cpp}`** — read-only parser for `%APPDATA%\nvda\nvda.ini`
  (ConfigObj sections / `[[subsections]]`).
- **`menus_modsettings.cpp`** — the submenu. Nested list views are now a table
  (`k_nestedViews`: Voice settings, Combat announcements); add a nested list =
  add a row table + a `NestedView` entry.
- **`probe_memory_sound.cpp`** — dev probe, KOTOR 1 only: F11 a 3 s pulsed tone
  3 m left, Shift+F11 four Eloquence phrases around the listener.

## Placement (2026-10-28, the swoop beacons' method)

The first build played each line AT its subject inside a 4 / 30 m engine
band. Two problems surfaced in the first test: lines far off faded to near
silence, and past 30 m the engine refused the source, so the line jumped to
loud centred screen-reader speech — a cliff right at the edge of most fights.
Now, as `minigame_swoop_audio.cpp` "Decoupled cue" does for the pads:

- The source sits `kPlaceRadiusM` (5 m) from the engine's listener (camera;
  the cursor in view mode) in the subject's direction, inside a flat engine
  band (full to 20 m), so the engine supplies only the pan.
- Direction and distance are measured from the player (view-mode cursor)
  against the camera yaw (`view_mode::GetEffectiveViewpoint`), like every
  spoken bearing. A subject within 1 m (a hit on the player) is straight
  ahead.
- Loudness is ours, over the real horizontal distance: full within 4 m,
  linear down to 0.4 at 30 m (swoop's far-pad floor, 50/127), 0.4 beyond.
- From 20 m to 30 m the bearing eases to straight ahead, so a far line ends
  centred with no step in level or direction. Nothing is refused for range
  any more; the centred fallback is left for real failures.

All constants are first guesses at the top of `positional_speech.cpp`.

## Voice choice (normal channel)

`positional_speech.cpp` `ChooseNormalVoice`, cached 2 s:

1. Active screen reader not NVDA (`prism::ActiveScreenReader()`) → urgent
   voice. JAWS etc. cannot be imitated (their voices live inside them).
2. NVDA `[speech] synth`:
   - `ibmeci` and the engine file exists → Eloquence, settings from
     `[speech] [[ibmeci]]` (voice = ECI dialect id, variant, rate, rateBoost,
     pitch, inflection, hsz, rgh, bth, sampleRate) and paths from
     `[ibmeci] TTSPath / dllName` (relative to the add-on's synthDrivers).
     Mapped like the add-on driver (`synthDrivers/ibmeci.py`, `_ibmeci.py`):
     rate % → ECI speed 40..156, ×1.6 with rateBoost.
   - `sapi5` / `oneCore` → `[[sapi5|oneCore]] voice` is a registry token path;
     its default value is the display name ("Microsoft Stefan - German
     (Germany)"), read in the 32-bit view (WOW6432Node). Prism voice names
     match when equal or a leading part ("Microsoft Stefan"). Rate % passed
     to Prism 1:1 — exact for SAPI's -10..+10, approximate for OneCore
     (Prism's curve: 0.5× / 1× / 6× at 0 / 0.5 / 1).
   - anything else (eSpeak, …) → urgent voice.
3. A named render that fails → urgent voice; that fails → centred.

NVDA settings are the SAVED ones: NVDA has no API to read its live settings.
They change on NVDA+Control+C or on exit (with save-on-exit). Every line
re-reads them, so a save takes effect on the next line — tested.

## Traps already hit (do not repeat)

- **The screen reader's volume does not belong here at all.** ECI volume 55
  baked into the render sounded "super quiet" next to NVDA's 55: NVDA plays
  at full system level, the game through its own mixer. Applying NVDA's
  volume as source volume instead was still too quiet once distance came on
  top (user, 2026-10-28). Every voice now renders at FULL volume (urgent too)
  and only the mod's own combat-volume sliders set the level. Don't
  reintroduce mirroring: the two volumes live in different reference frames.
- **ECI call order: callback BEFORE output buffer.** Re-registering the
  callback per line after `eciSetOutputBuffer` dropped the buffer: ECI then
  played every line ALOUD through the sound card (loud, centred, real time,
  1.5-2 s per line) and returned no samples — it sounded like a doubled
  screen reader. The callback is registered once at handle creation and
  finds the line via `g_current`; a line that ends with no samples discards
  the handle.
- **ECI delivers callbacks as window messages** to the thread that created
  the handle; the worker pumps its own queue until the end-mark index
  (0xffff) comes back. A fresh handle per line costs ~110 ms, so the handle
  persists (re-created only when the dialect changes).
- **Never render on the urgent channel's own SAPI voice.** Prism's
  speak-to-memory is a synchronous `Speak` on the same `SpVoice`, so it would
  queue behind (and block the game on) urgent speech still playing.
  `prism_registry_create` builds fresh render-only instances;
  `prism_registry_acquire` would return the cached urgent one.
- **Targets are not only creatures.** The first test shot a security door:
  creature-only lookup found nothing and every line went centred.
- **Play's return value is not proof of sound** (see `IsVoiceAttached`).
- **A static `LoopSource` faults harmlessly in its dtor at game exit** (the
  engine is gone; SEH-caught, logged "dtor faulted"). Seen with the probe.

## Logs (grep)

- `PosSpeech: normal voice: <kind> <name>` — voice choice changed.
- `PosSpeech: queue <ch> subject=[..] handle=.. voice=.. state=..` — taken over.
- `PosSpeech: play <ch> at (x,y,z) vol=N <ms>` — playing in 3D.
- `PosSpeech: fallback (<why>)` / `nothing named [..] to place` — centred.
- `Eloquence: render ok|failed ... in N ms` — expect ~100 ms.
- `Speech: render[SAPI|OneCore] ...`, `Speech.spoke: [positional] ...`.

## Open

- KOTOR 2 in game (values built in from decompiles; see the K2 section of
  `in-memory-sound-resource.md`).
- Placement tuning (4 m full, 0.4 floor at 30 m, centring 20..30 m), and
  whether lines should be dropped or
  shortened when the normal queue backs up in long fights (today: kept, in
  order — the screen reader queued the same way).
- Kill-line victim slicing per language (Polish / Russian word order).
- Portable NVDA (config outside %APPDATA%) is not found → urgent voice.
- Behind-you cue BUILT 2026-10-28, always on (user decision), untested: a
  high-shelf CUT above 3 kHz (RBJ, slope 1, audio_wav ApplyHighShelf) ramping
  from 0 at the side to -10 dB straight behind, applied after the peak
  normaliser so a line behind is also a little quieter. Taken from Dark Vision
  (the WotR blind mod, github.com/bradjrenshaw/darkvision-wotr,
  src/Audio/Spatializer.cs — also where the positional-TTS idea came from;
  both credited in the v0.9.0 changelog). Not a low-pass: that erases bright
  cues, and a dry/wet blend comb-filters. Needed even with dsoal: generic
  HRTFs confuse front and back without head tracking. Log: `rear=` dB on
  every `PosSpeech: play` line.
- Audio path CONFIRMED on the user's KOTOR 1 (2026-10-28 `Audio.Path` lines,
  diag_settings.cpp): dsound.dll is dsoal from the game folder, OpenAL loaded,
  the game opens Miles' "Creative Labs EAX (TM)" provider (handle read at
  CExoSoundInternal +0xf8 matched Miles' own list), so 3D sounds — ours too —
  reach dsoal as 3D buffers; alsoft.ini has stereo-encoding=hrtf. Not
  confirmed: OpenAL's runtime HRTF state — its log can't be switched on from
  our DLL (dsound.dll and its OpenAL load before our DllMain; both read the
  env switches there), only from the launching shell.

---

# 3D exit descriptions (built 2026-10-28, untested)

The walking region labels ("Kreuzung: Nord, Tür Süd-West nach X", "Bereich
Ost-West, Ausgänge: ...") spoken with each EXIT sounding from where it is.

## Decisions (with the user, 2026-10-28)

- Mod settings: the "Combat announcements" submenu became "Positional
  announcements"; it holds two toggles, "3D combat announcements"
  (`PositionalCombatSpeech`) and "3D exit descriptions"
  (`PositionalExitSpeech`, default ON like its sibling). The four sliders stay
  shared: they belong to the two voices, not to a feature. The urgent pair
  drives the exit lines too, and their labels say so.
- The WORD stays what it was: the octant from the zone centre (plus the
  "far in the north" add-on). The SOUND comes from the exit's real position
  relative to the player and the camera. Word = where in the zone, sound =
  which way to turn. They rarely disagree; when they do, the sound is the
  truth from where you stand.
- An exit right next to the player (the one just walked through) plays from
  its true direction, no special case. Revisit only if it causes problems.
  Within 1 m there is no direction at all and the piece plays straight ahead,
  the same as combat.
- Far exits: the guidance beacon's soft knee (`guidance_beacon.cpp`): honest
  loudness up close, a faint but always placed far field, and the bearing kept
  at EVERY distance. Combat's 20..30 m easing to straight ahead is not used.
- A single axis word ("Korridor Nord-Süd", "Bereich Ost-West") stays one word,
  played centred with the region name. Only real exit lists are placed:
  junction and area exits, doors, transitions, corridors that name both ends,
  a dead end's way out.
- Own voice slot (`Channel::Exits`, announcement voice, urgent sliders). A new
  region replaces the old exit list, as SpeakUrgent's interrupt did. When
  combat starts or a blocking menu opens (`transitions::IsWorldSpeechGated`),
  a playing exit list is cut off. After combat the region refire waits until
  both combat voices are quiet, not just the fixed 1.5 s.

## How it is built

- `room_topology` records an `ExitMark` {entry text, point} at every place a
  region label appends an exit entry, through a sink that `PassClassifyClusters`
  arms around `ClassifyCluster`. The marks are stored per node next to
  `node_label`; `LookupAt` hands them out through an optional out parameter.
  The label text is untouched: marks are only recorded, never spliced in.
  Points: an area exit's in-zone anchor (the far-qualifier anchor), a corridor /
  junction / dead-end exit's outside neighbour node, a door's position, a
  transition trigger's centre.
- `positional_speech::SpeakExitList(text, exits)` splits the final spoken text
  at the marks (searched in order, whole entries only: an entry followed or
  preceded by a letter or '-' is not a match, so "Nord" never matches inside
  "Nord-Süd"). Text between marks plays straight ahead at full level; each
  exit plays from its point. Pieces render one at a time as they come up, so
  walking into a junction costs one SAPI render per tick, not five at once.
  Unmatched marks are dropped with a log line; no match at all still plays the
  whole label through the slot (straight ahead), so one region label never
  overlaps another.
- Consumers: `transitions.cpp` (region change, the delayed Platz announce,
  post-combat refire) and `view_mode.cpp` (the cursor's region). The map
  screen cursor and the degrees key stay centred.

## Logs (grep)

- `PosSpeech: exits [..] -> N pieces, M placed` — the split.
- `PosSpeech: exit mark not found in text: [..]` — a mark the text lost.
- `PosSpeech: play exits at ... dist=.. gain=..` — a piece playing.
- `PosSpeech: exits cut (..)` — replaced by a new region, or gated.
