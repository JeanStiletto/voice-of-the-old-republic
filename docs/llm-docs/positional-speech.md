# Positional combat speech — how it works

Combat announcements spoken FROM the world position they are about, through
the engine's own 3D sound path. Built and tested 2026-10-27 (KOTOR 1 tested in
game; KOTOR 2 built with decompile-witnessed values, not yet tested).

The engine mechanism underneath (playing a WAV that has no file) is in
`in-memory-sound-resource.md`. This doc is the feature on top of it: what it
does, how the pieces fit, the decisions behind it, and the traps already hit.

---

## What the player gets

- Mod settings → "Combat announcements" (`Kampfansagen`) submenu → toggle
  "3D positioned combat announcements" (`PositionalCombatSpeech`, persisted in
  `acc_settings.ini`, default ON).
- Each spoken combat line sounds from where its effect lands, with the
  engine's panning, distance falloff, SFX slider and EAX/dsoal reverb.
- Normal lines imitate the screen reader's voice where possible; urgent lines
  use the announcement (SAPI) voice the user picked.
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
  - Level chain: peak-normalise to -1 dBFS, then the voice's own volume
    setting (`RenderedSpeech::volume`) as the per-source game volume
    (127 = full). Distance band: full within 4 m, silent by 30 m
    (`kFullVolumeM` / `kSilentM`) — first guesses, tunable.
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

## Voice choice (normal channel)

`positional_speech.cpp` `ChooseNormalVoice`, cached 2 s:

1. Active screen reader not NVDA (`prism::ActiveScreenReader()`) → urgent
   voice. JAWS etc. cannot be imitated (their voices live inside them).
2. NVDA `[speech] synth`:
   - `ibmeci` and the engine file exists → Eloquence, settings from
     `[speech] [[ibmeci]]` (voice = ECI dialect id, variant, rate, rateBoost,
     pitch, inflection, volume, hsz, rgh, bth, sampleRate) and paths from
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

- **Volume baked into the render is far too quiet.** ECI volume 55 sounded
  "super quiet" in game next to NVDA's 55: NVDA plays at full system level,
  the game through its own mixer. Fix: render at FULL volume, hand the
  setting back (`RenderedSpeech::volume`) and apply it as source volume after
  peak normalisation. (The urgent channel's SAPI volume IS baked in on
  purpose — the urgent-volume slider governs those lines.)
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
- Distance band tuning (4 m / 30 m), and whether lines should be dropped or
  shortened when the normal queue backs up in long fights (today: kept, in
  order — the screen reader queued the same way).
- Kill-line victim slicing per language (Polish / Russian word order).
- Portable NVDA (config outside %APPDATA%) is not found → urgent voice.
- Front/back separation is the engine's stereo limit; a rear-darkening filter
  on our own PCM (WotR's trick) is an idea, not built.
