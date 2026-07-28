# passive_narrate.cpp (484 lines)

Hooks `CClientExoAppInternal::ShowObject` (via the exported `OnShowObject`
trampoline at the bottom) — the engine's single user-facing target-change
signal, fired by both mouse-hover auto-target (`DoPassiveSelection`) and the
Q/E cycle (`SelectNearestObject`). On a real transition it resolves the
handle, classifies it into one of six nav categories (`filter_objects`),
speaks a localized name + combat-brief enrichment (`combat_query`), plays a
3D positional cue (`audio_bus`/`audio_cues`), stamps `narrated_target`, and
records it for `discovery`. Also implements a two-stage retry for the
sentinel-handle ("no target after LOS pruning") case on Q/E presses: defers
one tick to let the engine's candidate halo rebuild, then synthesizes the
same Q/E key through `CClientExoAppInternal::HandleInputEvent` directly.
Talks to `narrated_target`, `spectator_scene` (Endar Spire scripted-battle
line), `same_name_suffix`, `engine_area`, `strings`, `prism`.

## Declarations (in source order)

- L26 — `volatile uint32_t s_show_object_handle`
  note: namespace-scope (not anonymous) so the extern-"C" OnShowObject handler can update it; single-threaded, no atomic.
- L53-59 — `struct QEState { press_active, direction_code, retry_armed, retry_wait, inside_retry }; QEState s_qe`
  note: direction_code caches 204(E)/205(Q) so the retry can resynthesize the same key.
- L78-80 — `constexpr DWORD kAutoRenarrateQuietMs = 10000; uint32_t s_last_auto_handle; DWORD s_last_auto_tick`
  note: suppresses repeat auto-narration when engine focus flickers target->sentinel->target (e.g. lingering neutral creature); explicit Q/E bypasses entirely.
- L84 — `acc::audio::NavCue ClosedDoorCueForMaterial(void* obj)`
  note: mirrors cycle_input.cpp's mapping; keep in sync or lift into filter_objects.
- L95 — `acc::audio::NavCue CueForCategory(acc::filter::CycleCategory c, void* obj)`
- L112 — `acc::strings::Id CategoryNameId(acc::filter::CycleCategory c)`
- L128 — `acc::filter::CycleCategory ClassifyForNarration(void* obj)`
  note: returns Count_ for non-nav objects (combat/dialog targets).
- L143 — `bool IsActivePartyMember(void* obj)`
  note: compares server handle against GetPartyMembers() (follower table); PC is never a passive-narrate focus so followers-only is correct.
- L165 — `bool NarrateHandle(uint32_t handle, const char* reason, bool explicitRequest)`
  note: party members get no person cue ever; on auto-focus path they're fully suppressed, on explicit Q/E only the cue is dropped. Auto path is debounced by s_last_auto_handle/tick.
- L267 — `void OnEngineShowObject(uint32_t handle)`
  note: DEADBEEF sentinel = "no announcement yet this DLL load" (suppresses save-resume speech); sentinel-handle path arms a one-tick retry on first sighting, speaks CycleNoTarget on the second (retry) sighting.
- L368 — `void RequestQEReannounce(int directionCode)`
  note: deferred to next Tick because our input detour fires before the engine's Q/E handler calls ShowObject.
- L382 — `bool IsInSynthesizedQE()`
  note: read by input_pipeline to avoid re-arming press_active on our own synthetic HandleInputEvent call.
- L386 — `void Tick()`
  note: path 1 drains sentinel-skip retry via a direct CClientExoAppInternal::HandleInputEvent(0x00621210) call (__thiscall(void*,int,int), SEH-guarded, dir 204/205); path 2 re-announces on single-hostile-combat no-transition case.
- L480 — `extern "C" __declspec(dllexport) void __cdecl OnShowObject(void*, int)`
  note: thin trampoline for the ShowObject detour declared in hooks.toml @ 0x005f9c8e.
