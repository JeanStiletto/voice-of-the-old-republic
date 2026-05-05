# Engine Action Picker — driving context-sensitive interactions

**Status: first implementation landed 2026-05-05** — picker drives Enter, falls back to `AddUseObjectAction` if the engine returns no descriptor. Untested in-game. Follow-up to commit `c680ceb`.

## Premise

**Use the engine's own context-aware action picker as the default for every "Enter on focused target" dispatch.** Do not imitate per-kind action logic ourselves.

When a sighted player hovers a cursor over an object, KOTOR's engine inspects the object's state (locked / hostile / trapped / has-key / friendly / dead / …) and the active leader's capabilities (Security skill, Bash, equipped weapons, dialog availability, …) and computes one default action — the one the cursor sprite shows and the one a single left-click executes. That picker is the system we want to drive.

The current `AddUseObjectAction` dispatch is a fallback that hits a single hardcoded action id (`ACTION_USEOBJECT = 0x28`). It works for "open unlocked door / talk to standing NPC / loot unlocked container" because those happen to be the engine's default action for those object states. It silently fails for everything else: locked doors (need Security), hostile NPCs (need Attack), trapped placeables (need Disable Trap), computer terminals (need Repair / Computer Use), security-locked footlockers, swoop terminals, mines, … . The list is open-ended and grows with content modules — every Manaan computer, every Korriban tomb trap, every Tatooine swoop registration is a custom interaction. Hand-coding a per-kind picker against vanilla content would parallel-implement what the engine already does and would still need a fresh patch for every new mod's content.

## Goal

Single Enter-on-target dispatch that reproduces what a left-click on the focused target does for a sighted player. Whatever default action the engine picked for the cursor, we pick the same. No per-kind branching in our patch.

Acceptance: the Endar Spire locked door (Trask + Security) opens via Enter without our patch knowing the word "Security". When that works, the same code unlocks every following content blocker for free.

## What we know

### Established surfaces (verified live in earlier sessions)

- **`CClientExoApp::GetLastTarget` `@0x005EDD80`** *(known)* — read-side of "what is the engine considering targeted right now". Populates organically as the player walks past interactables; matches Q/E/Tab cycle output.
- **`CClientExoApp::SetLastClickedOnTarget` `@0x005EE200`** *(known)* — write-side of the "last clicked" handle. Used by the engine's own click handler.
- **`CClientExoAppInternal::HandleMouseClickInWorld` `@0x00620350`** *(known signature, broken when called direct)* — `__thiscall(void)`, no args; reads target + descriptor from internal state and dispatches the action against the active leader. Direct call from our patch produced "dispatched cleanly with zero engine response" — see `docs/navsystem-progress.md:51` and commit `d578fbe` post-mortem.
- **`CClientExoAppInternal::DoPassiveSelection(float delta) @0x005FA5A0`** *(known, character-frame-driven)* — populates passive-selection state on a tick basis. Probe `patch-20260504-063846.log` proved this is **not** cursor-coord-driven; it's character/camera-frame driven (which is why the cursor-warp `MoveMouseToPosition` workaround failed).
- **`CSWGuiManager::MoveMouseToPosition @0x0040C790`** *(known, GUI-only)* — moves cursor + walks `HitCheckMouse` + `UpdateMouseOverControl` on the GUI side. **Does not reach world-hover state.** Eight-warp probe verified.
- **Action descriptor at `+0x4c8`** *(suspected, location confirmed by retired-path post-mortem, layout unknown)* — the slot the click handler reads to know what action to run. Populated by the cursor-hover system (which DoPassiveSelection feeds). Empty when we synthesised a click without the prior hover. **Owning struct not yet pinned down** — the comment in `interact_hotkey.cpp:215` puts it on "the manager" but neither `CClientExoAppInternal` nor `CSWGuiManager` is explicitly RE-confirmed for this offset.

### Negative results to not re-litigate

- Cursor warp does not populate world-hover. *Verified — `patch-20260504-063846.log`.* `MoveMouseToPosition` only touches GUI manager fields, not the world-hover/passive-selection pipeline. Don't try this again.
- Direct `HandleMouseClickInWorld` call without descriptor population is silent. *Verified — commit `d578fbe`.* The function executes, returns, no engine state change.
- Setting `LastTarget` via `SetLastClickedOnTarget(handle)` alone does not populate `+0x4c8`. *Inferred from commit `d578fbe`.* The two operations are decoupled in the engine; the descriptor is computed from passive selection, not from the last-clicked target.

## What we resolved (2026-05-05, this session)

Decompiled `HandleMouseClickInWorld @0x620350`, `GetDefaultActions @0x620620`, `DoPassiveSelection @0x5fa5a0`, `SelectNearestObject @0x5fb050`, `SetMainInterfaceTarget @0x62b000`, `PopulateMenus @0x689d80`, `CSWGuiInterfaceAction::CSWGuiInterfaceAction @0x4eae30`, and `ServerToClientObjectId @0x5eda50` from Lane's Ghidra DB. Cross-referenced against `swkotor.exe.h` for struct layouts.

### O1 — *Resolved.* Descriptor lives on `CClientExoAppInternal`.

- `field292_0x4c8` = `CSWGuiInterfaceAction*` — pointer into the per-target action array.
- `field293_0x4cc` = `int` count, gates the dispatch (`(int)count > 0 && desc != 0`).
- `CSWGuiInterfaceAction` layout (`swkotor.exe.h:5437`, stride 0x38):
  - `+0x00` `CExoString label` — engine-localised verb ("Sicherheit", "Sprich", "Öffne", …)
  - `+0x08` `ulong action_id` — engine action enum (0x404 noop, 0x3ea talk, 0x3f7 use placeable, 0x3f5 bash, 0x3f2 toggle door, 0x3f4 disable mine, 0x3eb security?, …)
  - `+0x0c` `void* action_function` — function pointer (e.g. `CSWCDoor::ToggleDoorState`, `CSWCCreature::ActionInitiateDialog`, `CSWCPlaceable::UsePlaceable`/`BashPlaceable`, `CSWCDoor::MenuActionBash`, `CSWCTrigger::ActionMenuDisableMine`)
  - `+0x1c` `ulong target_id` — client-side handle (high bit 0x80000000 set)
  - `+0x20` `CResRef icon` — engine icon name ("i_dialog", "i_opendoor", "i_useplace", "i_attack", "i_disablemine", "i_noaction", …)

`HandleMouseClickInWorld` dispatch (when the gate matches): `(*(action+0xc))(*(action+0x8), playerCharId)` after `CClientExoApp::GetGameObject(*(action+0x1c))` validates the target. So the action function is called with `(action_id, player_creature)` — likely `__cdecl` or `__stdcall`. We do not call action functions directly.

### O2 — *Mooted.* We bypass `DoPassiveSelection` entirely.

`SelectNearestObject` (Q/E cycle) sets `last_target` but does NOT touch `field292_0x4c8`. `DoPassiveSelection` updates `field283_0x4a4` + the descriptor for the camera-framed target. The cycle and hover paths are independent.

We don't need to coax `DoPassiveSelection` to pick our target — `GetDefaultActions(this)` reads `gui_in_game->main_interface->field1_0x64` (the "main interface target") to decide which target to compute actions for. We set that explicitly via `CGuiInGame::SetMainInterfaceTarget` and then call `GetDefaultActions` ourselves.

### O3 — *Resolved.* The engine's own picker entry point is `CClientExoAppInternal::GetDefaultActions @0x00620620`.

Reads `main_interface->field1_0x64` (the hovered target id), `CSWParty::GetPlayerCharacter` (the leader), then `CSWCCreature::GetInterfaceTargetType(leader, target)` to switch into per-kind branches: NONE / DOOR_OR_PLACEABLE / TARGET_SWITCHING / MINE / ... Each branch allocates 1-2 `CSWGuiInterfaceAction` entries into `field292_0x4c8` with the appropriate verb, action_id, function pointer, and icon. So the engine's *picker* is a single function we can call after pointing `main_interface->field1_0x64` at our target.

### O4 — *Partially open, not blocking.* Descriptor lifecycle.

- `field292_0x4c8` is freed and re-allocated on every `GetDefaultActions` call (the function `_eh_vector_destructor_iterator_`s the prior array, frees, then `Allocate`s the new size). So calling it back-to-back is safe — each call re-derives.
- The dispatch branch in `HandleMouseClickInWorld` clears `last_clicked_on_target = 0x7f000000` immediately before calling the action function. So one Enter press = one action.
- We don't yet know what clears `field283_0x4a4` between hovers; not relevant since we overwrite it ourselves.

### O5 — *Partial.* Engine action ids enumerated by what we saw in `GetDefaultActions`:

- `0x404` — NONE (no-op, "i_noaction")
- `0x3ea` — talk to creature
- `0x3f2` — open/close door
- `0x3f4` — disable mine
- `0x3f5` — bash/attack
- `0x3f7` — use placeable
- `0x5fb` — recover mine (TLK strref)

Full enum still unmapped, but **we don't need to read it on the dispatch path** — the engine reads `+0x08` itself. We only log it for diagnostics.

## What we built (first version)

`patches/Accessibility/engine_picker.h/.cpp` — `acc::picker::Drive(serverHandle, &snapshot)`:

1. Convert server handle → client handle (`server | 0x80000000`).
2. Walk `AppManager → CClientExoApp → CClientExoAppInternal → gui_in_game → main_interface`.
3. `CGuiInGame::SetMainInterfaceTarget(guiInGame, targetClient)` — installs hover target.
4. `CClientExoAppInternal::GetDefaultActions(internal)` — populates `+0x4c8` / `+0x4cc`.
5. Snapshot the descriptor (label, icon, action_id, target_id, count) for narration + log.
6. Write `last_target = last_clicked_on_target = field283_0x4a4 = targetClient` to satisfy the dispatch gate.
7. `SetPlayerInputEnabled(false)` (auto-restore in 3s) and `CClientExoAppInternal::HandleMouseClickInWorld(internal)` — engine dispatches the picked action.
8. SEH-wrapped at every step; on any fault we restore input and log.

`interact_hotkey.cpp::OnInteract` now:
- Calls `picker::Drive` first.
- Pre-roll uses the engine's own localised verb (snap.label) when descriptor is valid: "Sicherheit Türschloss" instead of "Öffne Türschloss".
- Falls back to `AddUseObjectAction` if the engine returned an empty descriptor or faulted — the simple "open / talk / pick up" cases keep working as before.

`strings.{h,en,de}` — new `FmtInteractEngine = "%s %s"` (engine-verb + target-name).

## What's untested

- The whole path. No in-game test yet. First test target: Endar Spire's locked door (Trask's tutorial) — should now speak the engine's "Sicherheit" verb and dispatch the security action via `HandleMouseClickInWorld`.
- Whether `field283_0x4a4` accepts the same handle form as `last_target` (server handle OR'd with 0x80000000). Decompile suggests yes.
- Whether `HandleMouseClickInWorld`'s opening gates (`pCVar2->field45_0xb4 == 0`, `gui_in_game->field12_0x30 == 0`, `client_options->camera_mode != 5`) are typically open during normal in-world input. Logs will reveal a silent fall-through.
- Whether action functions called by HandleMouseClickInWorld correctly enqueue against the active leader (the same Tab-leader question that motivated `c680ceb`'s diagnostic).

## Polish followups (post-test)

- Replace the per-kind `PreRollFor` mapping when we confirm the engine label always reads cleanly. Currently it's still the fallback when descriptor is empty.
- Wire `picker::ReadCurrent` into a passive narrator so that hovering a target with the cursor (or holding cycle focus while the cursor happens to overlap) speaks the engine's chosen action without pressing Enter.
- If `HandleMouseClickInWorld` consistently dispatches cleanly, retire the `AddUseObjectAction` fallback in `interact_hotkey.cpp` — single dispatch path, no per-kind code anywhere.
- Map remaining `action_id` values from `GetDefaultActions` so log lines are interpretable without cross-referencing the decompile.

## Anti-pattern to avoid

Do not add per-kind action selection in our patch. Concretely, `interact_hotkey.cpp` should not grow:

- `if (cat == Door && IsLocked(door) && HasSkill(leader, Security)) dispatchUseSkill(...)`
- `if (cat == Container && IsLocked(...)) ...`
- `if (cat == Npc && IsHostile(...)) dispatchAttack(...)`

Each branch is a parallel implementation of the engine's picker for one object kind. Maintenance load grows with content. The first time a mod adds a custom-scripted placeable, our picker silently picks "Open" and the user is stuck again.

The doc-on-failure for that anti-pattern is the current state: `AddUseObjectAction` is the simplest possible per-kind hardcode — "always pick action 0x28" — and it's already broken on the first locked door of the first tutorial of vanilla.

## Out of scope (explicit)

- **Radial menu UI.** That's the override surface for picking a non-default action. We don't need it for the default-action goal; it's a separate pillar.
- **Hover narration.** Reading the descriptor for narration polish is a nice extension (item 5 above) but not the goal of this investigation.
- **NWScript-level dispatch.** `ExecuteCommand*` routines are slower than direct `Add*Action` calls. We'd reach for them only if the direct primitives prove unreachable, and only after O5 lands the action-id enum.

## Carried-forward references

- `docs/navsystem-progress.md` lay-off 9b post-mortem — original retirement of `HandleMouseClickInWorld`.
- `docs/navsystems-investigation.md` Q6 §"RE — does MoveMouseToPosition trigger world-hover?" — eight-warp probe data.
- Commit `d578fbe` — switch from click-pipeline to `AddUseObjectAction`.
- Commit `c680ceb` — Tab-leader + Enter-dispatch diagnostic; `GetPlayerCreature` confirmed to track the active leader.
- `patches/Accessibility/interact_hotkey.cpp:24-37` — preserved RE notes for `SetLastClickedOnTarget` / `HandleMouseClickInWorld`. Kept in source even though unused by the active dispatch path.
- Memory: `feedback_discovery_doc_format.md` — known/suspected/open structure used in this doc.
- Memory: `feedback_explain_decisions_step_by_step.md` — checkpoint after each investigation step rather than batching.
