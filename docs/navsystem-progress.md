# Navigation System — Implementation Progress

> **Document role:** execution state, not design. The design lives in `docs/navsystem-longterm-plan.md` and changes rarely; this file changes every commit and tracks *where we are in building the plan*.
>
> **Update rule:** every commit / lay-off point / new bug discovery → update the relevant section of this file as part of the same commit. A fresh session should be able to read this file alone and know exactly what to do next.

---

## Phase status

- **Phase 0 — Refactor.** *In progress.* First lay-off landed; further extractions and the menu regression test still pending.
- **Phase 1 — Foundation.** Pending.
- **Phase 2 — Playable baseline.** Pending.
- **Phase 3 — Pillar 1.** Pending.
- **Phase 4 — Pillar 2 polish + view mode.** Pending.
- **Phase 5 — Pillar 3 polish.** Pending.
- **Phase 6 — Map markers & nice extras.** Pending.
- **Phase 7 — User options UI.** Deferred per plan.

---

## Active phase: Phase 0 — Refactor

### Goal
Extract `core/` and `engine/` foundations out of the monolithic `Accessibility.cpp`; split menu code into `menus_*.cpp`. Plan-mandated layout decision: **flat with subsystem-prefix filenames** at the patch root (not literal `src/core/` subdirectories) — see plan decision log, dated 2026-05-03.

### Lay-off log

**Lay-off 1** — committed `71b155e` ("Accessibility: Phase 0 (lay-off 1) — extract core_dllmain + engine_input").

Extracted from `Accessibility.cpp`:

- `engine_input.{h,cpp}` — `acc::engine::InputIndexName` + `ManagerTranslateCode` + `kInput*` logical codes (Up/Down/Left/Right/Enter1/Enter2/Esc1/Esc2/Activate)
- `core_dllmain.cpp` — `DllMain`, `OnRulesInit`, `EnsureTolkInitialized`, `kModVersion`, `g_versionSha`

Build verified (`kdev build` clean, 5 .cpp files, all exports verified).

Discipline: this is a **mid-phase lay-off**, not Phase 0 exit. Hand-off rule = commit + fresh session for next extraction.

### Still to extract (future Phase 0 sessions)

Each is its own session (single-topic discipline):

- **Lay-off 2 — `engine_reads.{h,cpp}`.** Pull SEH-guarded readers and the offset constants out of `Accessibility.cpp`. Specifically: `CallDowncast`, `ReadControlNameFields`, `ReadCExoString`, `ReadU32`, `LookupTlk`, `ReadGuiString`, `ExtractTextOrStrRef`, `ExtractTextOrStrRefIndirect`, `ReadToggleState`, `IsToggle`, `IsSlider`, `IsListBox`, `DumpControlVtable`. Plus `engine_offsets.h` carrying the constant pile (`kButton*Offset`, `kLabel*Offset`, `kVtable*`, `kPanelControlsOffset`, `kListBox*Offset`, `kAddrTlkTablePtr`, `kAddrGetSimpleString`, `struct CExoString`, `struct CExoArrayList`, etc.).
- **Lay-off 3 — `engine_panels.{h,cpp}`.** `PanelKind` enum, `kPanelKindOffsets[]`, `PanelKindName`, `ResolveGuiInGame`, `IdentifyPanel`, `IsPanelKindInGameMenu`, the panel-kind cache.
- **Lay-off 4 — `engine_manager.{h,cpp}`.** `kAddrGuiManagerPtr`, `kMgrPanels*Offset`, `kMgrModalStack*Offset`, `GetForegroundPanel`, `FindOwningPanel`, `LogManagerStack`, the `MoveMouseToPosition` / click-sim PFN typedefs and addresses.
- **Lay-off 5 — Rename `Accessibility.cpp` → `menus.cpp`.** Everything left after the engine extraction lands here unchanged. Per plan: incremental refactor — don't decompose the menu-side logic in Phase 0.
- **Lay-off 6 — Menu regression test.** User runs `kdev dev`, walks the menu paths (main menu, Options tabs+sliders+cycles, in-game menu icons + sub-screen drill, dialog reply selection). Phase 0 cannot exit until this is reported clean. Crash investigation (see below) is *not* a regression — pre-existing — but its disposition before declaring Phase 0 exit is up to the user.

### Current file inventory (`patches/Accessibility/`)

- `manifest.toml` — patch manifest (id, version, supported game hashes)
- `hooks.toml` — detour bindings (6 active hooks, 4 disabled diagnostics)
- `exports.def` — DLL exports (6 functions: OnRulesInit / OnHandleFocusChange / OnHandleInputEvent / OnSetActiveControl / OnListBoxSetActiveControl / OnUpdate)
- `log.{h,cpp}` — file/debug logging primitives (unchanged since pre-plan)
- `tolk.{h,cpp}` — screen reader bridge, lazily loaded (unchanged since pre-plan)
- `core_dllmain.cpp` — DLL entry + Tolk init plumbing *(new in lay-off 1)*
- `engine_input.{h,cpp}` — input code translation *(new in lay-off 1)*
- `Accessibility.cpp` — everything else (~3580 lines after lay-off 1; will shrink across lay-offs 2-4 and rename to `menus.cpp` at lay-off 5)

---

## Open bugs / known issues

### Crash: chargen Class screen, c0000409 stack canary

Status: **pre-existing instability**, not introduced by Phase 0. Reproduces both before and after lay-off 1. Documented here so the next session that addresses it has a complete picture.

**Repro path (one of two observed):**
- Title screen → Neues Spiel
- Through pre-game movie / first chargen panels until the Class panel (`CHARAKTERAUSWAHL` / `Wähle deine Klasse.`)
- Press Down arrow → crash within ~1 tick

A second symptom — audio stutter when pressing *Schließen* in Options — has been observed but recovery happens (process survives). May be a related but milder manifestation of the same audio-thread stress.

**Crash signature:**
- Exception code `0xc0000409` (`STACK_BUFFER_OVERRUN` / `__fastfail`) — security-cookie failure, *not* a regular access violation
- Faulting thread EIP: our DLL, RVA `0x2c9e` (= `0x10002C9E` at static base)
- Instruction at fault: `mov ecx, [ecx+14h]` — the c_string pointer read inside `CAurGUIStringInternal`
- `ECX = 0xae0f1673` at fault — non-null but garbage

**Call chain (frames in our DLL):**
- Frame [00] `ReadGuiString` (entry `0x10002C30`) — fault at the `[guiString+0x14]` read
- Frame [01] `ExtractTextOrStrRefIndirect` (entry `0x10001EA0`) — caller after `lea eax, [esi-4]` (the `guiStringPtrOffset = cexoOffset - 4` derivation)
- Frame [02] `ExtractAnnounceableText` (return at `0x100014D7`) — at the exit of step 2, the AsButton branch, with the canonical button offsets `0x16C / 0x174 / 0x1BC` pushed before the call

**What the log shows (`patch-20260503-162139.log` tail):**
- Chain rebind on the chargen Class panel (`074BB1C0`) builds 7 entries
- 6 of the 7 navigable entries have vtable `0x73E658` — the image-only-button class also used by InGameMenu icons and portrait-pickers
- State-flag reads in the chain dump show garbage:
  - `Chain [3] 074BB940 ... bit_flags=0x8000780e`
  - `Chain [4] 074BBDF8 ... is_active=2147533827`
  - `Chain [5] 074BBB9C ... bit_flags=0xaaa77e0e`
  - `is_active` is a 0/1 field; `bit_flags` is single-digit normally — these are random memory
- "Speculative read miss" events fire repeatedly against these same six controls during chain rebind and again during per-tick monitoring
- Last log line is mid-extraction; no panic / shutdown line follows

**Root-cause hypothesis:**
- Chargen Class buttons (vtable `0x73E658`) are reaching our chain in a state where their fields read as garbage (freshly-allocated-not-yet-initialized, or torn-down-and-reused — same pattern documented for InGameMenu icons in pre-existing memory)
- `[control + 0x168]` (`gui_string` ptr) returns a non-null garbage value
- `ReadGuiString` reads `[garbage + 0x14]` → access violation
- `__try / __except` in `ReadGuiString` *should* absorb this, but the strlen-style scan loop directly after the first deref likely walks into unmapped memory before SEH unwinds
- During unwind the stack canary is found corrupted → `__fastfail(2)` → c0000409

**Why finishing Phase 0 will not fix this:**
- Lay-offs 2-5 are pure code-relocation. They preserve exact behavior of `ReadGuiString`, `ExtractTextOrStrRefIndirect`, `ExtractAnnounceableText`, the speculative-read paths, and the per-tick monitoring. None of those change.
- Fix requires actual change to engine-read code: tighter probe before deref (e.g. `IsBadReadPtr` / `VirtualQuery` of the candidate `gui_string` page; OR `__try` inside the strlen scan; OR bypass speculative reads on vtable `0x73E658` controls when their other state fields look corrupt). All three are candidate strategies; pick one in a dedicated session.

**Recommended fix-session approach:**
1. Open as its own session ("debug one bug at a time" discipline).
2. Decide one strategy from the three above — prefer probe-before-deref because it generalizes to other speculative-read sites.
3. Single small change to `ReadGuiString` (or its replacement once `engine_reads.cpp` exists), test against the same chargen Class panel, commit.
4. If the fix lands after lay-off 2 has extracted `engine_reads.cpp`, it goes there directly. If before, it goes into `Accessibility.cpp` at the existing site and migrates with lay-off 2.

**Artifacts:**
- Patch log: `<game install>/logs/patch-20260503-162139.log`
- Crash dump: `C:\Users\fabia\AppData\Local\CrashDumps\swkotor.exe.14140.dmp`
- Disassembly snapshot used for analysis: `build/acc_dll.disasm.txt` (regenerate with `dumpbin /DISASM patches/.../accessibility.dll` if rebuilt — the file is overwritten by each run)

---

## Next session: where to start

Pick exactly one:

- **Continue Phase 0 (recommended for incremental progress)**: open a fresh session, claim "Phase 0 lay-off 2", extract `engine_reads.{h,cpp}` + `engine_offsets.h` per the spec under "Still to extract" above. Do **not** mix in any crash fix — the fix is its own session.
- **Address the chargen crash**: open a fresh session, claim the bug above by name, follow the recommended fix-session approach. Phase 0 work pauses until the fix is committed.

Either way: update this file at session end with the new state.
