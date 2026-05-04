# Engine Action Picker — driving context-sensitive interactions

**Status: investigation — opened 2026-05-05.** Follow-up to commit `c680ceb`. Owner: next session.

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

## What's open

These are the blockers for next session, ordered by what unblocks what.

### O1 — Where exactly is `+0x4c8` and what does it hold?

- *Open.* Comment thread in `interact_hotkey.cpp:215` names the offset but not the owning type. Two candidates: `CClientExoAppInternal` (where `LastTarget` and the click handler live) or `CSWGuiManager` (where the cursor lives). Field layout unknown.
- Resolves: whether we can read the descriptor from a hook and route on it.
- Investigation path: dump bytes at `HandleMouseClickInWorld` entry to see which `this` field it dereferences for the descriptor. The function is `__thiscall(void)` so the descriptor lives somewhere in `this+offset`.

### O2 — Does `+0x4c8` get populated for cycle targets (Q/E/Tab) or only for camera-framed passive-selection?

- *Open.* `DoPassiveSelection` is character/camera-frame-driven. Q/E target cycle (`SelectNearestObject @0x005FB050`) populates `LastTarget` but might not populate the descriptor slot — the two paths could be independent. If they're independent, even with cycle focus on the right target the descriptor would stay tied to whatever passive-selection picked.
- Resolves: whether cycling-then-pressing-Enter is enough, or whether we additionally have to coax the engine to pick the cycle target as its passive-selection focus.
- Investigation path: read the suspected descriptor slot every tick after a cycle press and log it; correlate against passive-selection output.

### O3 — Can we force the engine to evaluate a specific target through its picker?

- *Open.* If O2 lands "no, descriptor only follows passive selection", we need a way to say "engine, compute the default action for *this* target as if I had hovered it". Two candidate paths:
  - Find the function `DoPassiveSelection` calls internally to compute the descriptor for one object — that's our injection point.
  - Manipulate the inputs `DoPassiveSelection` reads (camera frame? proximity gate? whitelist?) so it picks our cycle target the next tick.
- Resolves: the dispatch path itself.

### O4 — Descriptor lifecycle.

- *Open.* When does `+0x4c8` clear? Per-tick? On click? Persistent until next hover? Affects how aggressively we have to time the populate-then-click sequence.
- Investigation path: tick-rate logging of the descriptor slot across known-state transitions (approach door → cycle to door → press Enter → after dispatch).

### O5 — Action ids beyond `ACTION_USEOBJECT`.

- *Suspected, not formally enumerated.* NWScript names exist (`ActionUnlockObject`, `ActionAttack`, `ActionUseSkill`, `ActionPickUpItem`, …) but the binary `ACTION_*` enum values are needed if we end up reading the descriptor and re-dispatching directly.
- Resolves: optional fallback path — if we can read the descriptor's action id but can't trigger the engine's own dispatcher, we can still dispatch the named action ourselves. Less generic than driving the picker but still avoids per-kind logic in our patch.
- Investigation path: extract from Lane's Ghidra DB (`k1_win_gog_swkotor.exe.gzf`, not pulled locally per `CLAUDE.md` — pull into `docs/llm-docs/re/` first).

## Suspected investigation order

This is the cheapest-first ordering of next-session work. Each step is a checkpoint — stop and reassess after.

1. **Pin owning struct of `+0x4c8`.** *(O1)* Disasm the prologue of `HandleMouseClickInWorld @0x620350`; the first few instructions will deref `this` → some field. That field is the descriptor base. ~15 min if Ghidra DB is at hand.

2. **Dump descriptor layout.** *(O1)* Once O1 has the offset+owning-struct, pattern-match against known click handlers (the engine's own click code reads the descriptor too — its access pattern reveals the field shape: action_id, target_id, sub-skill, item, etc.). ~30 min.

3. **Tick-rate logging probe.** *(O2 + O4)* Hook `DoPassiveSelection` exit and log the descriptor slot. User walks past the locked door, cycles to it with Q/E/Tab, holds before pressing Enter — we capture whether the descriptor reflects cycle focus or only spatial hover. ~one in-game session.

4. **Decision branch:**
   - If descriptor follows cycle focus organically → **route Enter through `HandleMouseClickInWorld`** (re-enable the retired path, this time with confirmed pre-state). Single dispatch primitive replaces `AddUseObjectAction`. *(O3 collapses.)*
   - If descriptor only follows passive selection → **find `DoPassiveSelection`'s inner action-computation call**, bind it to our cycle target. *(O3 expands into O3a: locate inner call.)*

5. **Polish:** descriptor read provides the picked action id and target name — use it to refine our pre-roll text (instead of always saying "Öffne Tür", say "Sicherheit Tür" / "Angriff" / "Sprich" based on what the engine actually picked). This is a free win once we can read the descriptor, even before we can drive the click.

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
