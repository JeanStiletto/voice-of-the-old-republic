# passive_narrate.cpp (239 lines)

Implementation of passive-selection narration. Delta-detects ShowObject transitions, resolves + classifies handles through the six-category filter, plays 3D cues, speaks enriched names (with combat brief), stamps narrated_target. Also exports the ShowObject detour trampoline.

## Declarations (in source order)

- L19 — `namespace acc::passive_narrate`
- L23 — `volatile uint32_t s_show_object_handle = 0x7F000000u;`
  note: exposed at namespace scope (not anonymous) so the extern-"C" OnShowObject handler can update it
- L25 — `namespace { // anonymous`
- L27 — `bool s_qe_reannounce_pending = false;`
- L31 — `acc::audio::NavCue ClosedDoorCueForMaterial(void* obj)`
- L42 — `acc::audio::NavCue CueForCategory(acc::filter::CycleCategory c, void* obj)`
  note: door cue depends on open_state + material; other categories ignore obj
- L59 — `acc::strings::Id CategoryNameId(acc::filter::CycleCategory c)`
- L74 — `acc::filter::CycleCategory ClassifyForNarration(void* obj)`
  note: returns Count_ for non-nav targets (combat / dialog targets the cycle filter rejects)
- L88 — `bool NarrateHandle(uint32_t handle, const char* reason)`
  note: shared by both the focus-change path and the deferred Q/E re-announce path; resolves, classifies, plays 3D cue, speaks enriched name, stamps narrated_target
- L148 — `} // namespace (anonymous)`
- L150 — `void OnEngineShowObject(uint32_t handle)`
  note: DEADBEEF sentinel = "no announcement yet this DLL load"; suppresses first-tick handle to avoid save-resume noise; Q/E pending flag drives sentinel-branch spoken feedback
- L215 — `void RequestQEReannounce()`
- L219 — `void Tick()`
- L233 — `} // namespace acc::passive_narrate`
- L235 — `extern "C" __declspec(dllexport) void __cdecl OnShowObject(void* /*clientObject*/, int handle)`
  note: thin trampoline for the ShowObject detour hook at 0x005f9c8e; casts int handle to uint32_t and forwards
