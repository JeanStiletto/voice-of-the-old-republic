# Action menu, targeting & combat (RE reference)

Radial/personal action surfaces, the engine action picker, Q/E targeting + focus signals, bare-key dispatch, and combat-log internals.

> Migrated from the agent memory store on 2026-06-14 (memory-system cleanup).
> Each section below is one former memory note, preserved verbatim. Verify
> addresses/offsets against current code before relying.

## radial_populate_decomp
_TESTED 2026-05-05. How CSWGuiMainInterface::PopulateMenus drives target_action_menu — wrapper does the downcast, inner reads/writes field1_0x24 as 4×3 selection grid; action_lists.data is the read source-of-truth (rendered button text lags)_

Two `PopulateMenus` functions:

- **Wrapper @0x00689d80** — `CSWGuiMainInterface::PopulateMenus(this)`, no args. Public entry point. Called by `HandleMouseClickInWorld` first-click branch. Does: GetSWParty→GetPlayerCharacter; GetGameObject(this->field1_0x64); **`vtable->AsSWCObject` downcast**; refills 6 personal-action lists on main_interface (`field5_0x74[0..5]`) via `GetPersonalActions`; calls inner; updates UI from personal-action lists.
- **Inner @0x00689410** — `CSWGuiTargetActionMenu::PopulateMenus(this, player, mode, swc_target, *out)`. Refills `this->action_lists[0..2]` via `CSWCCreature::GetTargetActions(player, swc_target, row, &list)` for row=0..2. Sets `this->target_type` from `GetTargetInterfaceTargetType(player, swc_target)`. Reads `field1_0x24[target_type*3 + row]` to repaint each row's selected action.

`int field1[12]` at TAM +0x24 is a **4 target_types × 3 rows = 12 selection slots**. Each int = currently-selected `action_id` for that (target_type, row); `-1` = unselected (use `action_lists[row].data[0]`). SelectNextAction/SelectPrevAction *mutate* `field1_0x24[target_type*3 + row]` to advance/regress in-row. DoTargetAction reads it to find the action to dispatch.

Two different action sources: `GetPersonalActions(player, mode, &list)` feeds the surrounding MainInterface (6 mode-keyed lists, used for the 4 visible action slots in the lower-right HUD). `GetTargetActions(player, target, row, &list)` feeds the radial's 3 per-row lists (where Security on a locked door appears).

**Why:** Pinning these semantics makes it possible to drive the radial without re-RE'ing on every session. The wrapper-vs-inner split + field1_0x24 indexing was non-obvious from struct layouts alone — required reading SelectNextAction/SelectPrevAction/DoTargetAction together to see they all key on the same field1 slot.

**Read source-of-truth:** Right after the wrapper returns, `action_lists[r].data[k]` is fully populated but the rendered `target_actions[r].action_button.gui_string` AND inline `CExoString` are both `[]` — the engine paints them on a later tick. To get the row's selected label *now*, read `action_lists[r].data[selectedIndex].label` where `selectedIndex` is the entry whose `action_id` matches `field1[target_type*3 + row]`, falling back to `data[0]` when field1 is `-1` or no entry matches. Reference impl: `engine_radial::FindSelectedActionDescriptor` + `ReadRowActionLabel` fallback path (2026-05-05).

**Cursor-coupling (absorbs the former `project_radial_cursor_coupling` note):**
the engine re-derives the target menu from the live OS-cursor ray every update,
which breaks Shift+Enter for windowed/keyboard-only users. Fix = snapshot the rows
on open + lazy re-anchor only when the target rows actually drain — shipped in
`unified_action_menu.cpp` (see the HARD RULE in [[unified-action-menu-design]]).

**How to apply:** When extending radial behavior, never call inner @0x00689410 directly — write `main_interface->field1_0x64` and call the wrapper instead. To navigate within a row, use SelectNext/Prev (engine handles field1 mutation). To dispatch, use DoTargetAction. If a feature wants "preselect a specific row+action on open", write the action_id to `field1_0x24[target_type*3 + row]` AFTER the wrapper has run (so target_type is set). For any user-facing label read at populate time, use `action_lists.data[].label`, not the rendered button text. Decomp source: `docs/radial-menu-investigation.md` and `tools/ghidra-scripts/Decompile.java`.

---

## action_menu_engine_surfaces
_CSWGuiMainInterface.field45_0x771c[6] = 6 columns; per-column up/down buttons + DoPersonalAction; not the radial — separate UI, separate work_

The "AKTIONSMENÜ" the post-first-fight tutorial teaches is the **player's own action bar** strip — six columns of `CSWGuiMainInterfaceAction` widgets at offset `0x771c` of `CSWGuiMainInterface`. **This is NOT the radial** (`CSWGuiTargetActionMenu`, already accessible). Same column primitive `CSWGuiMainInterfaceAction` is reused in both — radial has 3, main interface has 6.

Per-column widgets (size 0x71c):
- `action_button` — icon, click to use current variant
- `action_label` — text of current variant
- `up_button` / `down_button` — cycle variant within column (tutorial's "Pfeile neben dem Symbol")

Engine functions (Lane's labels, GoG bytes match Steam):
- `0x006888e0` — `SelectPrevPersonalAction(this, int)` — backward cycle
- `0x0068ad60` — `DoPersonalAction(this, int, int)` — execute current variant; what hotkeys 4..7 invoke
- `0x00688690` — `OnCharacterClicked` — portrait click, sets which character bar shows for
- `0x00619db0` — `CSWCCreature::GetPersonalActions(slot, out)` — variant list source-of-truth

`SelectNextPersonalAction` not labelled — likely either adjacent to `0x006888e0` or `SelectPrev` takes a direction param. Don't assume; verify by hooking `up_button` OnPressed.

GUI events: `OnUpArrow=61`, `OnDownArrow=62`, `OnLeftArrow=63`, `OnRightArrow=64` (`swkotor.exe.h:16938`).

**Why:** Player got stuck post-first-fight tutorial that teaches "click portrait, scroll columns, mouse-wheel cycle, click to use Medikit." Hotkeys 4..7 already fire `DoPersonalAction(slot)` so blind player can already trigger column-current-variant — but has no way to know which variant or to cycle within column.

**How to apply:** Recommended path is to drive existing `up_button`/`down_button` widgets via click-sim (same infrastructure as in-game menu icons + equip slots) — avoids needing the unlabelled `SelectNext` address. Read `action_label` gui_string for announcements. Discovery doc: `docs/action-menu-investigation.md`.

---

## engine_action_picker
_TESTED 2026-05-05. To drive the engine's context picker, set main_interface->field1_0x64 → GetDefaultActions → click gate → HandleMouseClickInWorld. forceRadial=true skips dispatch and always opens the radial via PopulateMenus._

`patches/Accessibility/engine_picker.cpp` implements the dispatch path. End-to-end verified 2026-05-05 on Endar Spire Door B (Sicherheit). Sequence:

1. Walk `AppManager → CClientExoApp → CClientExoAppInternal → gui_in_game (+0x40) → main_interface (+0x90)`.
2. `CGuiInGame::SetMainInterfaceTarget(guiInGame, targetClient) @0x0062b000` — installs hover target for the picker.
3. `CClientExoAppInternal::GetDefaultActions(internal) @0x00620620` — populates `field292_0x4c8` (descriptor pointer, `CSWGuiInterfaceAction*`) and `field293_0x4cc` (count). Free + realloc internally → safe to call back-to-back.
4. Write `last_target = last_clicked_on_target = field283_0x4a4 = targetClient` (all client-side, high bit 0x80000000 set) — satisfies the dispatch gate in `HandleMouseClickInWorld`.
5. `CClientExoAppInternal::HandleMouseClickInWorld(internal) @0x00620350` — engine runs `(*(desc+0xc))(*(desc+0x8), playerCharId)`.

`CSWGuiInterfaceAction` (stride 0x38, `swkotor.exe.h:5437`):
- `+0x00` `CExoString label` — engine-localised verb ("Sicherheit", "Sprich", "Öffne")
- `+0x08` `ulong action_id` (0x404 noop / 0x3ea talk / 0x3f2 door toggle / 0x3f4 disable mine / 0x3f5 bash / 0x3f7 use placeable / 0x5fb recover mine)
- `+0x0c` `void* action_function`
- `+0x1c` `ulong target_id` (client-side)
- `+0x20` `CResRef icon` ("i_dialog", "i_opendoor", "i_useplace", "i_attack", "i_disablemine", "i_noaction")

**Why:** Resolves `docs/engine-action-picker.md` O1+O3. `SelectNearestObject` (Q/E cycle) only sets `last_target`; descriptor is `DoPassiveSelection`-driven (camera frame). Bypassing `DoPassiveSelection` and calling `GetDefaultActions` ourselves is the cleanest dispatch.

**How to apply:** When extending interact dispatch (Pillar 4, equip flow, target switching), call `acc::picker::Drive` rather than reaching for `AddUseObjectAction` or hand-rolled per-kind logic. `picker::ReadCurrent` reads the cursor's current descriptor without driving anything — useful for hover narration. `acc::guidance::UseObject` is still wired in `interact_hotkey.cpp` as the fallback for "Drive returned false" (chain unresolved / SEH fault).

**forceRadial overload** (added 2026-05-05): `picker::Drive(handle, snap, /*forceRadial=*/true)` skips the GetDefaultActions descriptor check and unconditionally calls `CSWGuiMainInterface::PopulateMenus(main_interface)`. Use when you want vanilla right-click semantics — show ALL applicable actions for a target, not just the highest-priority default. Bound to **Shift+Enter** in `interact_hotkey::PollHotkey`. Necessary because GetDefaultActions collapses multi-action targets (locked door with Bash + Security available) to `count=1` whenever any party member can perform the top verb, so the radial fallback would never fire for the cases users actually want it for.

---

## dopersonalaction_variant_selection
_param_2 is unused; the variant to fire is chosen by *(mi + 0x1bac + slot*4) — six int32s, one per column, holding the selected action_id_

`CSWGuiMainInterface::DoPersonalAction @ 0x0068ad60` (the function bare 4..7 ultimately invokes) ignores its `param_2` argument entirely. Variant selection is read from a per-column **selected action_id field** at struct offset `0x1bac + slot*4` (six int32s — `field7_0x1bac` through `field12_0x1bc0` in `swkotor.exe.h`). The function searches `field5_0x74[slot]` for a list entry whose `+0x08` action_id matches that field, and **falls back to `data[0]` on no-match** (including the sentinel `-1`).

**Why:** Without this knowledge, calling `DoPersonalAction(slot, 0)` always fires variant 0 regardless of cycle state, because no list entry's action_id equals 0. Bare 4..7 happens to work on mouse-driven play because `SelectPrevPersonalAction @ 0x006888e0` writes to this field whenever the user cycles via mouse-wheel / arrow button — so it's already pointing at the current variant when DoPersonalAction runs.

**How to apply:**
- To fire a specific variant, write its action_id to `*(mi + 0x1bac + slot*4)` first. `acc::engine_actionbar::SelectVariant(mi, slot, index)` does this — reads the descriptor at `field5_0x74[slot].data[index] + 0x08` and stamps the field.
- `PopulateMenus` re-assigns action_ids each rebuild, so any stamped value is invalidated by `PrepareBareDispatch` / Q/E target change / sub-screen close. Always re-derive the action_id from the current descriptor list, never cache it.
- The labelled `OnActionUpArrowPressed @ 0x0068af70` / `OnActionDownArrowPressed @ 0x0068afe0` cycle wrappers are unusable: (a) both gate on `param_1->is_active != 0` and the `field45_0x771c` widgets are uninitialised, (b) `OnActionDownArrowPressed` is mislabelled — its body calls `CSWGuiTargetActionMenu::SelectNextAction` on `this`, treating `main_interface` as a `target_action_menu`. Bypass them; stamp the field directly.
- For the bare-key path: stamp AFTER `PrepareBareDispatch` (PopulateMenus runs there) and BEFORE returning from our HIE hook (so the engine's cut-bytes-then-switch reads the fresh value).

---

## dotargetaction_variant_selection
_Sibling of project_dopersonalaction_variant_selection. Target rows pick variant via field1[target_type*3+row]; SelectActionInRow stamps it. Bare 1..3 needs restamp after PrepareBareDispatch to preserve Shift+N cycle position._

`CSWGuiTargetActionMenu::DoTargetAction @ 0x00689610` (the function bare 1..3 ultimately invokes) reads its variant from `field1[target_type*3 + row]` — twelve int32s at TAM +0x24, one per (target_type, row) pair. Each holds an `action_id`; DoTargetAction walks `action_lists[row]` looking for an entry whose `+0x08` action_id matches, and falls back to `data[0]` on no-match (including the sentinel -1).

This is the exact analogue of the personal-column `*(mi + 0x1bac + slot*4)` mechanism, with two wrinkles:
- The slot is two-dimensional: `target_type` (CSWGuiTargetActionMenu +0x1AEA, 0..3 = creature/door/placeable/trigger) crosses with `row` (0..2). Different target_types index different field1 slots, so the "currently selected" persists per-target-kind.
- The engine's `SelectNextAction @ 0x006865b0` / `SelectPrevAction @ 0x00686680` DO write to field1 (unlike the personal `OnActionUp/DownArrowPressed` wrappers which are unusable). Either drive them or stamp directly.

**Why:** Without this knowledge, bare 1..3 always fires `action_lists[row].data[0]` in keyboard-only play because field1 starts at -1 and there's no way to cycle. Shift+1..3 submenu's cycle position is also lost between Esc and the next bare-N press, because `PrepareBareDispatch` (called by input_pipeline on every bare 1..7) reruns `PopulateMenus` which reassigns action_ids — invalidating any previously-stamped value.

**How to apply:**
- New primitive `acc::engine_radial::SelectActionInRow(tam, row, index)` reads the descriptor at `action_lists[row].data[index] + 0x08` and stamps `field1[target_type*3+row]`. Same shape as `acc::engine_actionbar::SelectVariant`. Use it; don't drive SelectNext/Prev — direct stamping bypasses the engine's "walk from current selection" semantics which would compound shadow drift after PopulateMenus.
- For Shift+N submenu cycles: track a shadow index per row, stamp on every Up/Down, and stamp again on Open (in case PopulateMenus reset field1).
- For bare 1..3 (parity with bare 4..7): after `PrepareBareDispatch` in input_pipeline, re-stamp with `unified_action_menu::CurrentSelection(row)` (was target_action_menu before the menu merge) so DoTargetAction lands on the user's cycled variant. Without this restamp, the bare-key path always fires `data[0]`.
- target_type wrinkle: if the user cycles Shift+1 on a creature then presses bare 1 on a door, the shadow index ("position N in row 0") points at a different action semantically. The restamp still works (writes to the door's field1 slot at the shadow index), but the user hears a different verb because action_lists rebuilt differently. Refresh via Shift+N before bare-N if uncertain.

---

## engine_qe_filter_rules
_Full kind/geometry/LOS rules for KOTOR's native Q/E target cycle — populated by GetNearestObjects @0x4fc4c0, kind-filtered by ValidNearestObjectType @0x4f2c30, stepped by SelectNearestObject @0x5fb050 with CanSee LOS pruning_

Q/E walks `CClientExoAppInternal.field164_0x2a8` (12-byte entries: id at +0, cached-CanSee at +4). Populated each `DoPassiveSelection` tick from `CSWSCreature::GetNearestObjects(player, 30.0, 30.0, &front_cone, &halo)`, where `halo` (the second output) is what gets copied into `field164`.

**Geometry**: 30-unit radius, **360° halo** around player. The *front cone* (60° ±30°, from a point 4 units behind player) is built in the same call but used for the white-outline passive-selection list, **not** for Q/E stepping. Sort order: clockwise yaw delta from player facing.

**LOS**: applied lazily in `SelectNearestObject` via `CSWParty::GetPlayerCharacter(party)->CanSee(obj)`. Result cached in the +4 byte; if 0 the entry is `deleteAt`'d from `field164`.

**Kinds (from `CSWSCreature::ValidNearestObjectType @0x4f2c30`, switch on `object_type`)**:
- DOOR — when `+0x2cc == 0` (not hidden) and `+0x3c0 == 0` (not disabled).
- CREATURE — alive + (in visibility list OR `DoSpotDetection` passes). Stealth/sneak respected. Hostility flag (`*outFlag=1`) iff `GetAIStateReputation == 2`.
- PLACEABLE — when `.usable != 0`. Hostility flag iff `+0x340 != 0` AND reputation < 11.
- TRIGGER — **only** if `is_trap_ != 0` AND (already in trap-detection list OR faction match OR reputation ≥ 0x5a).
- default — 0 (excluded). So WAYPOINT, ITEM, **non-trap TRIGGER (incl. area transitions)**, ENCOUNTER, STORE, SOUND, AREA_OF_EFFECT are all out.

**Implication for our cycle design**: Q/E is the "normal-play interactable" channel — doors/NPCs/containers/usable-placeables. Spoiler-correct because the engine already handles stealth + trap detection. **Area transitions, waypoints, map-notes, ground items are NOT in Q/E** — they're the natural set for our own `,`/`.` cycle to surface (orientation channel, complementary to Q/E's action channel).

**Per-frame caller**: `DoPassiveSelection @0x5fa5a0` rebuilds the halo each tick and runs combat-side side effects (enemy_sighted state machine, auto-pause-on-hostile, tutorial windows). The Q/E stepper is read-only over the list it leaves behind.

---

## show_object_is_focus_signal
_passive_narrate hooks CClientExoAppInternal::ShowObject because LastTarget is multi-source (combat AI overwrites it every round); ShowObject is only called by user-driven paths_

`CClientExoApp::GetLastTarget` (field at CClientExoAppInternal+0x2b4) is a **multi-source bus** — written by `SelectNearestObject` (Q/E), `HandleMouseClickInWorld`, `SetLeader` (party swap), and crucially **`CSWSCreature::CreateNewAttackActions`** (every combat round, on every creature with a queued action). Per-tick polling against it races AI churn during combat and misses Q/E transitions entirely.

`CClientExoAppInternal::ShowObject @ 0x005f9c60` is the **single user-driven sink** — called only by `DoPassiveSelection` (mouse-hover auto-target, runs every frame as a refresh) and `SelectNearestObject` (Q/E cycle). It's the engine's "what's the red hostile-hilite ring currently on?" signal. AI churn never reaches it.

Hooked at 0x005f9c8e (post-branch merge where EAX holds the new target's id and EBX still points at the CSWCObject*). Cut is `MOV ECX, [ESI+0x40]; PUSH 0` — register/memory-relative move + immediate push, neither touches EFLAGS.

**Why:** Without this redirect, combat Q/E with a single hostile in range was completely silent — the engine's cycle was a no-op (nothing to cycle to), so polling saw no transition. Hooking ShowObject gave us a stable per-frame "current focus" signal we can delta-detect AND read on Q/E press for re-announce. Both cases now confirmed in 2026-05-21 trooper-fight capture.

**How to apply:** Anywhere we want "what is the user looking at right now" — Examine (Shift+H today still uses ReadLastTargetHandle), HUD overlays, target-aware action prompts — read `acc::passive_narrate::s_show_object_handle` instead of polling `last_target`. The cache stores the engine handle (high bit set for client-side); resolve via `engine::ResolveClientObjectHandle`.

**Q/E cycle:** the in-world hostile cycle is delegated to the engine (Q/E →
`SelectNearestObject` → ShowObject, which this hook watches); `,`/`.`/`-` were
reassigned to the map-side cycle. (Absorbs the former
`project_engine_native_qe_cycle` note.)

---

## select_nearest_object_sentinel
_Q/E cycle returns the no-target sentinel when CanSee LOS fails for every candidate at once — decompile-verified at 0x005fb050, NOT an angular-wrap quirk_

`CClientExoAppInternal::SelectNearestObject @ 0x005fb050` is the engine's Q/E (and mouse-look-toggle) hostile cycler. It reads a candidate list at `this->field164_0x2a8` (count at `field165_0x2ac`), advances through it by current direction, and calls `ShowObject(this, target, ...)` at `LAB_005fb3cd`.

**The sentinel path** (calls `ShowObject(NULL)` → our hook sees `0x7f000000`):

```c
if ((iVar8 == 0) || (local_c == 0)) {       // iteration exhausted
    if (iStack_4 == 0) {                     // no valid candidate found
        if (this->field165_0x2ac != 0) {
            return (int *)local_10;          // candidates remain → return WITHOUT ShowObject
        }
        // ALL candidates removed during iteration:
        this->last_target = 0x7f000000;
        pCVar3 = (CSWCObject *)0x0;
        goto LAB_005fb3cd;                   // ShowObject(this, NULL, ...)
    }
}
```

Sentinel fires only when the inner loop drained the candidate array to 0 elements. Three removal triggers, all hit `LAB_005fb32e`:
1. `CGameObjectArray::GetGameObject` returns miss (stale handle). Note: returns TRUE on miss — `project_csws_area_handles`.
2. `AsSWCObject` returns NULL (object exists but isn't a CSWCObject).
3. `CSWCCreature::CanSee(player, candidate) == 0` — LOS check from player character. Cached per-candidate at `+4`; `-1` = uncached, `0` = no, `1` = yes.

The CanSee path is the dominant cause in practice. When the player's pose makes the LOS check fail for every candidate at once, the list empties and the sentinel fires.

**The other no-narrate path**: when iteration ran out (`local_c == 0`) but candidates still remain (`field165_0x2ac != 0`), the function returns WITHOUT calling ShowObject. Our hook never fires; from the user's perspective the Q/E press has no effect.

**Filter mode `param_2`** (set by the caller, presumably by combat mode / context):
- `0` → LOS-only filter (CanSee check is the gate)
- `1` → vtable[78] check, falls through to LOS if the inner check returns 0
- `2` → CSWCCreature + vtable[78] + LOS chain

Direction `param_1`: 0 = forward (E), non-zero = reverse (Q). Wrap arithmetic at `LAB_005fb17c` is correct — there is NO angular-wrap quirk. The user-visible "press Q/E twice to wrap" symptom is the CanSee cascade, not bad wrap logic.

**Mitigation in our patch** (2026-05-29, verified Taris streets ES locale): two layers in `passive_narrate.cpp`.

1. **Safety**: on sentinel during a Q/E press we call `acc::narrated_target::Clear()` unconditionally. Without this, `narrated_target` stayed stamped on the previous real target and Enter / Shift+- / Ctrl+- / Alt+- / `-` would activate against stale focus while the user heard "no target".

2. **Auto-retry**: first sentinel after a user Q/E does NOT speak — instead arms `QEState.retry_armed` with `retry_wait=1`. Next `Tick()` (one engine frame later, after `DoPassiveSelection` has refreshed `field164`) synthesizes `CClientExoAppInternal::HandleInputEvent(direction, 1)` directly with `inside_retry=true`. The synthesized event routes back through our input-pipeline detour; `IsInSynthesizedQE()` makes `RequestQEReannounce` no-op so it isn't treated as a fresh first attempt. If the retry lands real → normal announce. If retry also sentinels → speak `CycleNoTarget` once, disarm. Result: a "wrap" beat between cycles is invisible to the user; only persistent-blocked targets surface as "Kein Ziel".

`QEState` shape: `press_active`, `direction_code` (204=E / 205=Q), `retry_armed`, `retry_wait`, `inside_retry`. All cleared on real-target announce.

---

## bare_combat_keys_dispatch
_KOTOR's bare 1-7 hotkeys require PopulateMenus to be fresh; vanilla only refreshes on combat-mode flip / sub-screen close / mouse hover, not on Q/E. We force a refresh in OnClientHandleInputEvent._

The vanilla KOTOR keyboard-only combat path is **broken by design**: pressing 1-3 (target actions) or 4-7 (personal actions) calls `DoTargetAction` / `DoPersonalAction` unconditionally on press, but those functions read `creature_id` from action-list items that were last stamped by `CSWGuiMainInterface::PopulateMenus`. Vanilla only invokes `PopulateMenus` on:
- combat-mode flip (`FUN_005f3a80` = `SetCombatMode`)
- main_interface re-added to manager (`MainLoop @0x603d42`, sub-screen close path)
- mouse passive-cursor hover (continuous hit-test rebuild)

**Q/E target cycle does NOT call `PopulateMenus`.** `SelectNearestObject @0x5fb050` → `ShowObject @0x5f9c60` → `SetMainInterfaceTarget @0x62b000` → `CSWGuiMainInterface::SetTarget @0x6855f0` only stores `field1_0x64 = id` and resets a refresh-hint float. Sighted users get away with the keyboard-only path because the mouse cursor keeps the action menu fresh; blind users hitting Q/E-then-1 land on stale action items whose `creature_id` no longer resolves, and the engine bails silently inside `DoTargetAction`'s `GetGameObject(creature_id)` gate.

**Why:** Decompile session 2026-05-21 confirmed the chain. Verified by `patch-20260520-205639.log` (broken: Critical Strike pressed many times, all `Combat.Attack` lines show normal hit/parry, never the +1d6 damage bonus the feat applies) and `patch-20260521-052856.log` (fixed: same press sequence, attack rolls now hit dmg=8/9 — well above the vibroblade's 1d6+STR=2-7 range, proving the feat consumed).

**How to apply:** Before extending bare-key dispatch behaviour (new keys, new categories), satisfy the engine's invariant by calling `acc::engine_actionbar::PrepareBareDispatch(targetClient)` first. The helper wraps `CGuiInGame::SetMainInterfaceTarget(guiIn, targetClient)` (0x62b000) + `CGuiInGame::RePopulateMainInterface(guiIn)` (0x62b050) under SEH and lives at `patches/Accessibility/engine_actionbar.cpp`. It's wired into `OnClientHandleInputEvent` (input_pipeline.cpp) so the engine's switch sees fresh `action_lists` by the time `DoTargetAction` / `DoPersonalAction` fires.

## Action-menu auto-pause (vanilla behaviour, decompile-confirmed 2026-06-14)

The vanilla radial/personal action menu does **NOT** pause the world by merely
opening. `CSWGuiMainInterface::PopulateMenus @0x00689d80` and
`HandleMouseClickInWorld @0x00620350` never call any pause function. The only
pause tied to the action menu is the optional **"Action Menu" auto-pause**
option, and it fires when the user *uses* the menu (cycles a row/column arrow):

- `CSWGuiMainInterface::OnTargetUpArrowPressed @0x006884b0` /
  `OnTargetDownArrowPressed @0x00688520` (radial rows) and
  `OnActionUpArrowPressed @0x0068af70` / `OnActionDownArrowPressed @0x0068afe0`
  (personal columns) all run the same gate before acting:
  ```c
  pCVar = GetClientOptions(client);            // CClientExoApp::GetClientOptions @0x005ed700
  if ((char)((uint)pCVar->bit_flags_2 >> 8) < 0)   // tests bit 0xf == 0x8000
      SetAutoPaused(client, 1, 7);             // CClientExoApp::SetAutoPaused @0x005edee0
  ```
- The setting lives in `CClientOptions::bit_flags_2` (+0x14). The six AutoPause
  checkboxes map to bits 0xb..0x10 (`CSWGuiInGameAutoPause::SaveOptions
  @0x006e73d0`): 0xb end-round, 0xc enemy-sighted, 0xd mine-sighted, 0xe
  party-killed, **0xf (0x8000) action-menu**, 0x10 triggers.
- **Default is OFF.** `CClientOptions::SetDefaultAutopauseOptions @0x0061d500`
  does `bit_flags_2 = bit_flags_2 & 0xffff77ff | 0x17000` — clears end-round
  (0x800) and action-menu (0x8000), sets enemy-sighted + mine-sighted +
  triggers. So out of the box the action menu does not pause at all.
- `SetAutoPaused` is the engine's *soft* auto-pause (sets `autopause_bit_flags_
  & 1` + the shared pause field `field206_0x37c |= 4`), distinct from the
  pause-key `SetPausedByCombat @0x005edc20` our `BeginOverlayPause` uses.
  There is no combat gate inside the arrow handlers — the gate is purely the
  setting bit.

**Our parity (unified_action_menu.cpp):** read the bit via
`acc::engine::GetActionMenuAutoPause` and only `BeginOverlayPause` on open when
it is set; remember it (`pausedOnOpen`) and resume symmetrically. When off, the
menu opens without pausing (matching vanilla) and Esc speaks an explicit
`ActionMenuClosed` cue since there is no pause-resume cue to ride on.

## Engine quirks worth remembering

- **Personal-key dispatch is LINEAR — there is NO 6↔7 slot swap.** (Corrected 2026-06-15; an earlier version of this note claimed an inversion — that was wrong, see below.) `DoPersonalAction` fires column `N-4` for key `N`:
  - key 4 → column 0 (Friendly Force / self powers)
  - key 5 → column 1 (Medical)
  - key 6 → column 2 (Misc / Sonstiges)
  - key 7 → column 3 (Explosives / Sprengstoffe)

  Subtlety that fed the old mistake: the **logical input codes** for keys 6 and 7 are themselves swapped — keyboard **6 emits `0xee`**, keyboard **7 emits `0xec`** (keys 4/5 are `0xe8`/`0xea`, sequential). So *by logical code* the dispatch is `0xe8`→col0, `0xea`→col1, `0xee`→col2, `0xec`→col3. The `input_pipeline.cpp` restamp switch is keyed on logical code and is correct as `0xec`→3, `0xee`→2.

  Our announce / menu-opener paths must be **linear** too (`risingK6`→col2 / `risingK7`→col3; `risingOpen3`→col2 / `risingOpen4`→col3 in `interact_hotkey.cpp`, and the F1 help list in `help.cpp`). They were previously *swapped* to match the mythical inversion, which made the spoken label point at the opposite column from what fired ("press 7 for Sonstiges, get an explosive"; on the Manaan seabed pressing 7 for the sonic emitter announced it but hit the empty Explosives column, while bare 6 used it silently).

  **Why the inversion was believed and how it was disproven:** the original RE compared the *key pressed* against our own **announce** (which was itself swapped), not against a real engine fire. Ground truth came from a clean seabed log (`patch-20260615-010243`, only the Schallgenerator present, in column 2): bare **6** produced `benutzt Schallgenerator` (column 2) while bare **7** routed to the empty column 3. When pinning a key→column mapping, verify against a `Combat.MsgBuf benutzt <item>` / `ADD [PLAYER]` line, never against our own announce.

- **Next-attack feats don't queue.** Critical Strike, Power Attack, Flurry, Power Blast etc. apply as **immediate stat modifiers consumed by the next attack**, not as `CSWSCreature.combat_round.actions` entries. So `Combat.SpecialWatch` will report `specials=0` even when these feats are firing correctly. The signal that they fired is the next `Combat.Attack` line carrying out-of-range damage (e.g., vibroblade rolling 8+ instead of the normal 2-7 cap).

- **Item-use actions DO queue.** Medikit (type=10), grenade throw (type=10), mine drop (type=10) push to `combat_round.actions` and are visible to `Combat.SpecialWatch`. `Combat.QueueRaw` logs each item per tick so a press of 5/6/7 with a real grenade/medikit should produce visible items.

- **Self-buff items target the player regardless of `field1_0x64`.** `GetPersonalActions(player)` stamps `creature_id=player` on items the engine classifies self-targeted (Medikit (Selbst), stims). Offensive items (grenades, blasters) use `field1_0x64` — so passing a sentinel (0x7F000000) when no narrated target is available keeps offensive items from firing at random while self-buffs still work.

## Diagnostic plumbing

- `ActionBar.Prep: target=0x... — SetTarget + RePopulate done` — one line per press, confirms the refresh fired.
- `Combat.QueueRaw [pN] item[i] type=0xXX target=0x... feat=0x... routine=0/1` — per-tick dump of every non-placeholder item in each party member's combat queue. Silent when the queue is empty (which is most of the time, because next-attack feats don't queue).
- `Combat.Attack: result=N dmg=M atk=[X] tgt=[Y]` — per-attack outcome. Damage above the weapon's normal max range is the strongest signal that a feat consumed correctly.

---

## appendtomsgbuffer_is_combat_log
_CGuiInGame::AppendToMsgBuffer @0x62b5c0 is THE live combat-log surface; AddMessages and messages_listbox are review-only_

`CGuiInGame::AppendToMsgBuffer @0x0062b5c0` (signature
`void __thiscall(CExoString*)`) is the single funnel every engine-emitted
combat-feedback string flows through during live play. Writes into a
64-slot, 16-byte-stride ring buffer at `this[+0xF8]` with write index at
`this[+0x100]`. Hooked at function entry with a 7-byte cut
(`57 8b f9 8b 4c 24 08` = PUSH EDI + MOV EDI,ECX + MOV ECX,[ESP+8]); see
`hooks.toml` and `combat.cpp::OnAppendToMsgBuffer`.

**Why:** the original `combat-system.md` plan named two wrong surfaces
that wasted a discovery cycle:

- `CSWGuiInGameMessages::AddMessages @0x626920` has exactly ONE caller
  per SARIF xref (`ShowDialogEntry`, dialog path). It populates
  `messages_listbox` when the review screen mounts; it is **never** on
  the live-combat path. Hook fires zero times during a full fight
  (`patch-20260521-095251.log`).
- `CSWGuiInGameMessages.messages_listbox @+0x64` is filled lazily at
  review-mount time, rebuilt from AppendToMsgBuffer's ring. During live
  combat the listbox row-count stays at 0; on panel open all rows
  arrive in a single-tick burst (`patch-20260521-093926.log`).

**How to apply:** when adding combat-log narration, special-event
filtering, dedup, or category routing — hook here, not on AddMessages or
messages_listbox. The string in `param_1` is already engine-localised
and verbosity-gated; no TLK lookup needed. For follow-on surfaces:
`CGuiInGame::AppendToDialogBuffer @0x0062b680` is the dialog twin
(same 64-slot ring pattern at `this[+0xFC]`), and `CGuiInGame::AddFloatyText
@0x0062b080` covers the floaty damage numbers above creatures' heads.

---

## combat_strings_extract_tool
_Tool that extracts MsgStrings engine anchors from any locale's dialog.tlk; cached locale TLKs live under data/dialog-tlk/; useful for any per-locale string parsing, not just combat_

`kdev combat-strings-extract [--tlk <path> | --lang <code>] [--output <file>]` emits a complete C++ `kXX` MsgStrings table for the locale of the supplied (or default) `dialog.tlk`. Source: `tools/kdev/Commands/CombatStringsExtractCommand.cs`.

**`--lang` shorthand:** resolves to `<projectRoot>/data/dialog-tlk/dialog_<code>.tlk`. The `data/dialog-tlk/` directory is a gitignored cache of per-locale `dialog.tlk` files captured via Steam language swaps — so we don't have to round-trip Steam every time we want to look at a non-DE locale's strings. Manifest at `data/dialog-tlk/MANIFEST.txt` records what's cached + each file's SHA-256. As of 2026-05-28: `dialog_en.tlk` cached; DE lives in the install root and doesn't need caching unless we expect future Steam-language-swap detours.

Engine-side anchors (21 fields) are reconstructed from strrefs in the 1xxx + 42xxx range — the mapping is in `StrrefMap` inside the command. Speech-side fields (`verb_hit` … `short_bonus`) cannot be auto-extracted and are emitted as kDe placeholders with `TODO(<lang>): translate` markers.

**Why:** The Steam `swkotor.exe` is locale-shared (verified 2026-05-28 via SteamDB depot inspection: one Windows base depot 32371 contains the exe, language depots only ship `dialog.tlk` + audio). So every combat-log string lives at a fixed strref across locales — extracting is mechanical once the DE-derived strref map exists. Same property makes the tool useful for ANY future per-locale string parsing — radial menus, UI labels, dialog text — just add new fields to `EngineAnchors` with their strrefs.

**How to apply (new locale):**
1. Steam → KOTOR → Properties → Language → switch to target → wait for `dialog.tlk` to download.
2. Copy `<game install>/dialog.tlk` → `data/dialog-tlk/dialog_<code>.tlk` and record SHA-256 in MANIFEST.txt.
3. Switch Steam language back to your own so the game's playable.
4. `kdev combat-strings-extract --lang <code> --output kXX.txt`.
5. Paste into `combat_strings.cpp` as a new `kXX` block; hand-translate the 15 speech-side fields.
6. Wire `Lang::Xx` in `strings.h`, add `lang_xx::Get` in `strings_xx.cpp`, add switch cases in `combat_strings.cpp::Get()` and `strings.cpp::Get()`.
7. Verify in-locale: trigger one combat scene, `grep 'MsgBuf: raw:' <install>/logs/patch-*.log`, confirm extracted anchors match.

EN done 2026-05-28: validated byte-for-byte DE round-trip + EN engine anchors extracted from cached `dialog_en.tlk`. EN speech labels hand-translated; one rough spot (BuildCompact emits "<actor> hits critical <target>" — German word order) flagged for post-tester polish.

---

