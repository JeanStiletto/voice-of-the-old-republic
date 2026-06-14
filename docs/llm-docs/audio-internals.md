# Audio & sound-source internals (RE reference)

Play3DOneShotSound gain chain, CExoSoundSource lifecycle, sound-mode pause exemptions, footstep paths, cue/party filtering, and droid-subtitle handling.

> Migrated from the agent memory store on 2026-06-14 (memory-system cleanup).
> Each section below is one former memory note, preserved verbatim. Verify
> addresses/offsets against current code before relying.

## prism_speak_voice_id
_Per-cue SAPI voice selection via prism::SpeakUrgent(text, voiceId); used to make compass turn-announce sound distinct from other urgent cues_

`prism::SpeakUrgent(const char*, size_t voiceId)` selects a SAPI voice
before dispatch. Caching is internal: consecutive calls with the same id
skip the `prism_backend_set_voice` round-trip. Out-of-range ids fall
back silently (we cache the voice count at SAPI acquire).

Why: NVDA-cancel-on-typed-char eats normal `Speak` while the user holds
WASD/A/D, but the SAPI bypass channel originally used a single voice so
every urgent cue (map cursor, walking cue, region cue) sounded identical.
Voice differentiation lets the user tell them apart without parsing
words mid-spin.

How to apply: every urgent cue currently passes `voiceId=0`. We tried
voices 1 and 2 to make compass/turn cues sound distinct, but the user's
NVDA-via-SAPI bridge mapped voice 1 back to NVDA (defeating the bypass)
and voice 2 was indistinguishable in practice on that box. Until the
urgent-cue surface grows enough to warrant differentiation, all callers
use voice 0. The voice-id parameter stays in the API so future cues can
opt into a different voice without an API rev — just be aware that any
non-0 id needs to be empirically tested on the user's setup before
adopting it.

Underlying API: Prism's `prism_backend_set_voice` is optional — older
prism.dll builds don't export it. The wrapper degrades to no-op (call
still speaks, just on whatever voice was last selected).

---

## play3doneshotsound_volume_amplifies
_How a one-shot cue's final loudness is computed; corrects the discredited \"4.0x amplifies\" claim. SetVolume clamps per-source byte to 127; group volume is the unclamped lever._


CORRECTS the earlier claim that Play3DOneShotSound's float arg amplifies >1.0.
That was wrong: the float was being fed into `fixed_variance` (a pitch field)
under a mislabelled typedef and then zeroed by the pitch-neutraliser detour, so
`kAccCueGain=4.0` did NOTHING to volume. `PlayCue3D`'s `volume` param is dead
(`(void)volume;` in audio_bus.cpp).

**Verified gain chain (Ghidra, 2026-06-01).** Final hardware amplitude is computed
in `CExoSoundSourceInternal::SetVolume` @0x005db800, re-invoked at playback from
`PlaySourceOn2DVoice`/`PlaySourceOn3DVoice`:
- `_Var7 = priority_group_volume × src_vol × factor / 127`  (factor=1.0 unless group 4 or 0x15-2D)
- 2D: `amp = _Var7 × sliderVol / 127`
- 3D: `amp = pow(_Var7 × sliderVol / 127, 0.6)`  ← loudness curve, boosts quiet, monotonic
where:
- `src_vol` = per-source byte = `field32_0x85`. One-shot `volume_byte` param writes it;
  **param=0 means SetVolume is never called → stays at ctor default 0x7f=127 (full).**
  `CExoSoundSource::SetVolume` HARD-CLAMPS to 0x7f. So per-source volume cannot exceed unity.
- `priority_group_volume` = byte copied from `priority_groups[group].volume` at ctor.
  Our 3D cues use group 0 → **vol=106 (NOT 127)** — they run at ~83% of unity, so there's
  headroom. Loaded from `prioritygroups.2da` `Volume` column, **stored as raw byte with NO
  clamp** (only PlaybackVariance clamps to [0,1]) → an Override 2da with Volume>127 stores
  >127 and multiplies in raw → above-unity at the struct level (Miles' final clamp UNVERIFIED).
- `sliderVol` = master SFX slider (`CExoSoundInternal.sound_effect_volume` float), global multiply.

**Priority group ≠ volume lever in isolation.** Group also sets max_player (voice budget),
interrupt, fade, and min/max_volume_distance falloff. Group 0: max_player=4, min_dist=20,
max_dist=10. See [[setsoundmode-priority-group-pause-exemption]] for the pause-exemption role.

**Levers to change cue loudness, in order of certainty:**
1. Attenuate / up-to-unity: pass `volume_byte` 1..127 to PlayCue/PlayCue3D (writes src_vol). CERTAIN.
2. Up to unity from current: put cue on a vol=127 group instead of group 0 (106→127, ~1.2×).
3. Above unity: Override `prioritygroups.2da` custom group Volume>127, OR pre-amplified WAV in
   Override\ keyed by resref. The WAV path is the guaranteed one; the 2da path is plausible but
   needs an in-game listen test (Miles may clamp the float to 1.0).

Runtime probe `probe_priority_groups.cpp` dumps the live table; group dump captured in
patch logs (entries 0-26 valid). Relates to [[project_play3doneshotsound_silent_resref]].

---

## play3doneshotsound_silent_resref
_Engine creates no source and produces no audio when a CResRef doesn't resolve in chitin.key — fire-counters can't detect it_

`CExoSound::Play3DOneShotSound` and `PlayOneShotSound` return successfully (no exception, no error indicator) when their `CResRef*` arg points to a string that doesn't resolve through the engine's `Override → streamwaves → streamsounds → streammusic → BIF/RIM` chain. The engine creates no `CExoSoundSourceInternal`, no audio thread work happens, and no sound plays — but our caller-side log lines (`CuePlayer: play cue=Wall ...`) keep saying "play" because we only check the function's return code.

**Why:** confirmed 2026-05-07 via per-fire `OnCalculatePitchVarianceFrequency` resref logging. The Wall cue's resref `gui_select` was missing from `chitin.key` for the entire install (only `gui_invselect` exists). 958 `play cue=Wall` log entries → 0 `gui_select` PitchHook fires that session, while every other cue (`fs_metal_droid2`, `gui_quest`, `mgs_s1`, `gui_close`, `gui_invadd`) had matching counts. Replaced with `as_nt_wtrdrip_09` after verifying it exists in `build/sounds-extracted-full/`.

**How to apply:** any time we add or change a cue resref, verify it exists in the full extraction at `build/sounds-extracted-full/` (or directly in `chitin.key` via `unkeybif l`) before shipping. Audible silence is the only symptom — the rest of the patch behaves as if the cue fired. The 16-char `CResRef` truncation makes >16-char resrefs silently fail the same way; longest current pick is `as_nt_wtrdrip_09` at exactly 16. The `OnCalculatePitchVarianceFrequency` heartbeat is a permanent canary — if a cue type fires hundreds of times but never appears in `PitchHook` lines, that resref is dead.

---

## cexosoundsource_loop_api
_Full lifecycle API for engine-managed looping/3D sound sources — alternative to fire-and-forget Play3DOneShotSound when continuous tones are needed_

CExoSoundSource is the engine's owned/managed sound source class. Unlike
CExoSound::Play3DOneShotSound (one-shot, no handle), CExoSoundSource is
constructed by the caller and kept alive for the full sound's lifetime,
making it the right primitive for loops, position-updated sources, and
anything that needs Stop().

**Why:** Investigated 2026-05-25 to answer "can we replace the swoop
obstacle 4 Hz ping cadence and the 2 s wall-scrape re-fire with a true
continuous loop?" — yes, the API exists and is fully decompiled.

**How to apply:** When a feature needs sustained spatial audio that
swells/pans naturally over time (wall scrape, obstacle approach,
beacon hum, ambient zones), reach for CExoSoundSource instead of the
PlayCue3D one-shot wrapper. Lifecycle:

1. `operator new` ~16 bytes (struct is vtable + internal*; 16 is safe)
2. Call ctor: `CExoSoundSource(this)` @ `0x005d5870` — sets vtable and
   allocates the 0xa0-byte internal via the engine's allocator
3. Configure (in any order, before Play):
   - `SetResRef(CResRef*, int)` @ `0x005d61c0`
   - `Set3D(int)` @ `0x005d5910` (1 = positional)
   - `SetPosition(Vector*, float)` @ `0x005d59e0` (float = z offset)
   - `SetPriorityGroup(uint8_t)` @ `0x005d5900`
   - `SetVolume(uint8_t)` @ `0x005d5950`
   - `SetLooping(int)` @ `0x005d59d0` (1 = loop)
4. `Play()` @ `0x005d5930`
5. Per-tick (if moving): `SetPosition(Vector*, 0.0f)`
6. Teardown: `Stop()` @ `0x005d5a20`, then dtor @ `0x005d60a0`, then free

Other methods worth knowing: `GetLooping` @ `0x005d6190`, `SetDistance`
@ `0x005d61a0`, `SetFixedVariance` @ `0x005d5a30`, `SetPitchVariance`
@ `0x005d5980`, `IsHardwarePlayingSoundPleaseDontUseThis` @ `0x005d5920`.

**Canonical engine usage pattern** (decompiled from CSWTrackFollower
::LoadSounds @ `0x0066f7e0` — the swoop bike's own engine-loop loader):
SetSoundName returns a CExoSoundSource* (the bike object owns a slot
array indexed by int). The bike then calls `SetLooping(src, 1)` and
the engine plays it automatically. So the canonical idiom is "configure
+ SetLooping" and the engine takes over.

**Gotcha:** ctor checks the global `DisableSound` flag and leaves
internal=null if true. All methods null-check internal and no-op, so
this is safe but means we should also check GetLooping() or similar
before assuming the source is live.

**Strong candidates for first use:**
- Wall scrape in swoop_race: one source per pin, position-updated per
  tick, Stop on release. Replaces the 2 s re-fire heartbeat with a
  continuous raspy tone.
- Obstacle proximity: one source per in-range obstacle. ~22 concurrent
  sources on m03mg — may stress voice budget; verify before committing.

Open questions:
- Which `operator new` does the engine use? Internal allocator vs MSVC
  CRT. For our outer-struct alloc (vtable + internal pointer, ~16
  bytes), CRT malloc/free should be safe since we're not freeing what
  the engine allocated.
- Voice budget pressure with many concurrent loops — needs live test.

---

## sound_mode_priority_group_pause
_Why a sound is silent under the in-game menu — SetSoundMode(4) pauses all CExoSoundSource except priority groups 1/2/0xb; GUI sounds use 0xb_


The in-game menu silences our cues not via the one-shot SFX path but via a
priority-group-selective pause on the whole sound subsystem.

**Mechanism (decompile-verified 2026-05-31):**
- `CGuiInGame::ShowSWInGameGui` @0x0062c9b0 calls `SetSoundMode(ExoSound, 4)` on open; `HideSWInGameGui` @0x0062cba0 calls `SetSoundMode(ExoSound, 0)` on close.
- `CExoSoundInternal::SetSoundMode` @0x005d5e80 → worker `FUN_005d8560`. Mode 4 (and 2/3) → `PauseAllSounds(0)`; mode 1 → `MuteSound` (global master fade, no exemption); mode 5 → `PauseAllSounds(1)` (pause everything); mode 0 → pop/restore previous mode.
- `PauseAllSounds` @0x005d82c0 with mode 4 pauses every source **except** those whose `priority_group` is **1, 2, or 0xb (11)**.
- `CExoSoundSource` ctor (`CExoSoundSourceInternal` ctor @0x005daf50) defaults `priority_group = 0x17 (23)` → NOT exempt → paused → silent. Volume defaults to 0x7f (full), so volume is never the issue here.
- `CSWGuiManager::LoadGuiSounds` @0x00409f00 builds the engine's own GUI click sounds and calls `SetPriorityGroup(src, 0xb)` — that's why menu clicks stay audible under the pause. `PlayGuiSound` @0x0040a140 plays them through the same `CExoSoundSource::Play` we use.

**How to apply:** Any cue that must be audible while an in-game sub-screen has the world paused must be a directly-driven `CExoSoundSource` with priority group **0xb** set before `Play()` (via `SetPriorityGroup` @0x005d5900, `__thiscall(this, byte)`). The play *path* (`CExoSoundSource::Play`) is fine; the group is the lever. This fixed the audio glossary audition (commit 72b367a) — `LoopSource::Start` gained an optional `priorityGroup` arg, glossary passes 0xb. The title screen worked without it because no paused world / no `SetSoundMode(4)` is active there.

Relates to [[project_messagebox_close_unpause]] (SetSoundMode(0) on popup close), [[project_cexosoundsource_loop_api]] (the source lifecycle API), [[project_play3doneshotsound_silent_resref]].

---

## rolling_footstep_is_vehicle_only
_Confirmed dead-end — the Rolling-prefixed footstep functions never fire for humanoid PCs; per-step `fs_*` audio comes from a different path_

`CSWCCreature::PlayRollingFootstepSound @0x006107c0`,
`UpdateRollingFootstepSound @0x00610840`, and
`LoadRollingFootstepSound @0x006105b0` are **for vehicles / wheeled units
(swoop bikes, droids on rollers)**, not for the per-step audio of
humanoid characters walking. Despite the name, hooking
`PlayRollingFootstepSound` produces zero handler fires during normal
in-game testing on Endar Spire — verified 2026-05-06 with the byte
probe at `0x006107c0` showing `e9 ?? ?? ?? ?? 90 90 90 90 …` (hook
correctly installed) and the diagnostic "first handler fire" log line
never emitted across full play sessions.

The actual humanoid per-step audio path is **not labeled in Lane's gzf**
and was not identified during the Phase 3 lay-off 5 RE budget
(2026-05-06). Likely candidate: an animation-event callback that calls
`CExoSound::Play3DOneShotSound @0x005d5e10` directly with a
surfacemat-derived `fs_*` resref, but the specific dispatcher hasn't
been located.

`CSWCCreature::DoFootstepAudio @0x0060ded0` is also unrelated — that's
a 5%-probability `as_cv_florcreak{1-3}` ambient floor-creak, not the
regular per-step audio.

**Why:** to keep us from re-hooking a name that looks right but doesn't
fire, and to anchor the next attempt on the chokepoint rather than the
"Rolling" lure.

**How to apply:** when revisiting Phase 3 lay-off 5 (footstep
suppression), don't start from `CSWCCreature::PlayRollingFootstepSound`.
Either (a) instrument `Play3DOneShotSound` with a resref logger to find
the actual caller's EIP, or (b) add a Ghidra xref-dumper script and
trace which of the ~hundreds of callers passes `fs_*` resrefs.

**RESOLVED 2026-05-06.** Path (a) worked: instrumentation hook at
0x005d5e16 logged 2465 fires across mixed walking, frequency-bucket
showed `0x0061a5b6` as THE per-step humanoid caller (203 fires across
7 `fs_metal_*` surface variants). `FindFunction.java` resolved that
EIP to `CSWCCreature::PlayFootstep @0x0061a2d0` — Lane HAD labelled it,
the "Rolling" name was just a lure. Suppression hook landed at 0x0061a31a
(engine's own field6_0x20==0 JZ) with `skip_original_bytes = true`.
See `audio_footstep_suppress.cpp` and the Lay-off 5 entry in
`docs/navsystem-progress.md`.

---

## cue_systems_party_filter
_Focus-narration cue (passive_narrate) and the continuous proximity beacon (spatial_change_detector) are separate; each needs its own party-follower exclusion._


There are TWO independent mod systems that emit a "person/Npc" sound cue
for nearby creatures — easy to confuse when a party-cue complaint comes in:

1. **passive_narrate.cpp** — fires `PlayCue3D` once on focus change
   (ShowObject delta / Q/E). Logs `PassiveNarrate: fire cue`. Already
   suppresses party via `IsActivePartyMember` (auto path: fully silent;
   explicit Q/E: name speaks, no cue).
2. **spatial_change_detector.cpp** — the CONTINUOUS proximity beacon
   (T1 per-sector + T2 foremost-in-front). Iterates `AreaObjectIterator`,
   classifies via `filter::ObjectMatches`, fires through
   `PlayCueAtPosition`. Logs `CuePlayer: play cue=Npc ...` and
   `ChangeDetector:`.

`filter::ObjectMatches` excludes only the **leader/PC**
(`GetPlayerServerCreature`), NOT party **followers** — deliberately, so
Q/E can still cycle onto companions. So system (2) beaconed companions
continuously while walking (in an empty area, the only fires). Fixed
2026-06-04: the scan loop fetches `GetPartyMembers` once per tick and
skips matching handles (server-side; matches `GetObjectHandle` on
iterated objects). See [[feedback_trust_dev_audio_reads]].

Diagnosing which system fired: grep `CuePlayer`/`ChangeDetector` (beacon)
vs `PassiveNarrate: fire cue` (focus). Related party-handle plumbing:
[[project_cserverexoapp_facade_split]].

---

## droid_subtitles_onomatopoeia
_Droid-language subtitles are onomatopoeia not translations; HK-47 speaks Basic; meaning carried by player replies_


Verified from extracted .dlg + binary dialog.tlk (German, lang 2) on 2026-06-05.

**T3-M4 and generic droids speak ONLY onomatopoeia in subtitles** — no
translated/meaningful text exists in their lines. TLK strings literally read
"Biep. Whoop. Wiep. Biep." (ebo_t3m4 strref 39353), "Beep. Beep. Beep."
(tar02_t3m4 22092), "Biep biebp biep-oop!" (k_hdroid1_dialog 38896). The
*meaning* is carried entirely by the PLAYER's reply lines (meaningful prose),
which the mod already narrates from the reply list. So we miss nothing by not
"translating" droidic.

**HK-47 speaks Basic** — full VO + meaningful German subtitles (race==DROID but
intelligible). Already spoken (droid appearance never suppressed by
[[feedback_first_sight_title_only]]-adjacent human-mask in dialog_speech.cpp).

**Inverse implication (minor):** dialog_speech.cpp never suppresses non-human
appearances, so it currently TTS-reads the beep-text aloud during T3
conversations. Noise, not lost info. If we want to suppress: key on
onomatopoeia content (small beep-word set, or Sound resref prefix "n_gendro_"),
NOT on race==DROID — that would wrongly silence HK-47.

**Tool:** build/dlgdump/ (.NET 10) — reads chitin.key/BIF + RIM/MOD + binary
dialog.tlk. Modes: `list <substr>`, `dump <resref>` (BIF), `find <substr>`
(scan modules/rims for DLG), `dumparc <archive> <resref>`. Reusable for any
.dlg subtitle/VO inspection. Companion party convos live in module archives
(ebo_m12aa.mod = Ebon Hawk), NOT in dialog.bif (only 32 DLGs there).

---

