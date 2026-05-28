# interact_hotkey.cpp (761 lines)

Interact hotkey implementation. Resolves target via narrated_target unified slot, classifies for pre-roll, drives engine action picker, falls back to UseObject. Also owns bare 1..3 target-key and 4..7 personal-key announce helpers, and all submenu routing (radial, actionbar, target-action, combat-queue, examine-view).

## Declarations (in source order)

- L36 — `namespace acc::interact`
- L42 — `static acc::strings::Id PreRollFor(acc::filter::CycleCategory c)` (anonymous namespace)
  note: maps cycle category to FmtInteractTalk/Open/Take verb
- L61 — `static acc::filter::CycleCategory ClassifyForInteract(void* obj)` (anonymous namespace)
- L86 — `static void* ResolveInteractTarget(uint32_t* outHandle)` (anonymous namespace)
  note: reads narrated_target unified slot; no fallback to engine LastTarget by design
- L108 — `static void DispatchInteractImpl(void* target, uint32_t handle, bool forceRadial)` (anonymous namespace)
  note: forward declaration; body at L150; calls picker::Drive then UseObject fallback
- L115 — `static void OnInteract(bool forceRadial)` (anonymous namespace)
  note: handles map-pin focus guard + no-target fallback; calls DispatchInteractImpl
- L303 — `static void AnnounceBarePersonalKey(int slot)` (anonymous namespace)
  note: speaks "{label} eingesetzt" for bare 4..7; reads actionbar_menu::CurrentSelection
- L371 — `static void AnnounceBareTargetKey(int row)` (anonymous namespace)
  note: speaks "{label} eingesetzt" for bare 1..3; reads engine_radial::ReadRowActionLabel
- L412 — `void DispatchInteract(void* target, uint32_t handle, bool forceRadial)`
  note: thin public forwarder into DispatchInteractImpl
- L416 — `void PollHotkey()`
  note: reads all rising-edge hotkeys; routes to submenu handlers or OnInteract
