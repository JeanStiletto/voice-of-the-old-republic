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

**Lay-off 2** — extract `engine_reads.{h,cpp}` + `engine_offsets.h`.

Extracted from `Accessibility.cpp`:

- `engine_offsets.h` — file-scope `constexpr` constants and engine-data structs:
  - GuiControlMethods vtable indices: `kVtableAsLabel`, `kVtableAsLabelHilight`, `kVtableAsButton`, `kVtableAsButtonToggle`
  - Button/label field offsets: `kButtonTextOffset`, `kButtonStrRefOffset`, `kLabelTextOffset`, `kLabelStrRefOffset`
  - Element-state offsets: `kButtonToggleStateOffset`, `kSliderMaxValueOffset`, `kSliderCurValueOffset`
  - CSWGuiText layout offsets: `kLabelGuiStringPtrOffset`, `kLabelTextObjectOffset`, `kButtonGuiStringPtrOffset`, `kButtonTextObjectOffset`, `kTextObjectTextOffset`, `kTextObjectStrRefOffset`, `kAurGuiStringCStrOffset`
  - Vtable identity addresses: `kVtableCAurGUIStringInternal`, `kVtableSlider`, `kVtableListBox`
  - Container offsets: `kPanelActiveControlOffset`, `kPanelControlsOffset`, `kListBoxControlsOffset`, `kListBoxBitFlagsOffset`, `kListBoxItemsPerPageOffset`, `kListBoxSelectionIndexOffset`, `kListBoxTopVisibleIndexOffset`, `kControlExtentOffset`
  - Engine structs: `CExoArrayList`, `CExoString`, `PFN_GetSimpleString`, `kAddrGetSimpleString`, `kAddrTlkTablePtr`
- `engine_reads.{h,cpp}` — `acc::engine::` namespace functions: `ReadControlNameFields`, `CallDowncast`, `ReadCExoString`, `ReadU32`, `LookupTlk`, `ExtractTextOrStrRef`, `ReadGuiString`, `ExtractTextOrStrRefIndirect`, `IsToggle`, `IsSlider`, `IsListBox`, `ReadToggleState`, `DumpControlVtable`

Convention follows `engine_input.h`: constants at file scope (callsite brevity); functions in `acc::engine::`. `Accessibility.cpp` adds `using namespace acc::engine;` so existing callsites compile unchanged.

Build verified (`kdev build` clean, 6 .cpp files, all exports verified). `Accessibility.cpp` shrank from 3626 → 3212 lines (~414 lines moved out).

Discipline: this is a **mid-phase lay-off**, not Phase 0 exit. Hand-off rule = commit + fresh session for next extraction.

### Still to extract (future Phase 0 sessions)

Each is its own session (single-topic discipline):

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
- `engine_offsets.h` — engine struct/vtable offset constants + `CExoString` / `CExoArrayList` *(new in lay-off 2)*
- `engine_reads.{h,cpp}` — SEH-guarded readers + element-class identity helpers *(new in lay-off 2)*
- `Accessibility.cpp` — everything else (~3210 lines after lay-off 2; will shrink across lay-offs 3-4 and rename to `menus.cpp` at lay-off 5)

---

## Open bugs / known issues

### Crash: chargen Class screen, c0000409 stack canary

Status: **fixed** in `ReadGuiString` (vtable check on `gui_string` before deref). Verified against the same repro path (`patch-20260503-170800.log`): chain rebuild on `CHARAKTERAUSWAHL` completes cleanly, all 6 vtable=`0x73E658` buttons return empty via the speculative-miss path, user navigates past the panel into the Endar Spire opening dialog without any SEH events. Kept here as the historical record because investigation overturned several earlier hypotheses.

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
- 6 of the 7 navigable entries have vtable `0x73E658`
- `Speculative read miss` events fire repeatedly against these same six controls during chain rebind and again during per-tick monitoring
- Last log line is mid-extraction; no panic / shutdown line follows

**Hypotheses overturned during investigation (kept so future sessions don't relitigate):**
- *"vtable `0x73E658` is an image-only-button override / different class."* Wrong. Lane's Ghidra DB labels `0x73E658` as `CSWGuiButton_vtable` — it's the standard `CSWGuiButton`, the same class as main-menu buttons. Our offsets (`gui_string` ptr at `+0x168`, etc.) are correct.
- *"`bit_flags` and `is_active` reading as garbage means the controls are uninitialized."* Wrong. The same garbage pattern (`is_active=2871141504`, `bit_flags=0xffff000e`, etc.) appears in the *successful* main-menu chain dump for working buttons whose text extracts fine. Those offsets (`+0x44` / `+0x4c`) are not actually `bit_flags` / `is_active` for every button instance — likely aliased / unused fields for some configurations. They're not a liveness signal.
- *"The strlen scan walks into unmapped memory."* Wrong. The fault is at `mov ecx,[ecx+14h]` (the second deref reading `gui_string`'s `c_string`), before the strlen loop runs.

**Actual root cause:**
- The chargen Class buttons reach our chain in a transient state where `[control + 0x168]` (the `gui_string` ptr) is sometimes null / safe and sometimes a non-null garbage pointer — the same controls successfully read empty during the chain rebind and crash on a subsequent monitor tick. The engine appears to mutate that field between our reads (a write-after-read race or partial deinit of the embedded `CSWGuiText`); we don't have clean instrumentation to identify the exact mutation point.

**Fix that landed:**
- `ReadGuiString` now checks `*(uintptr_t*)guiString == 0x00741878` (`CAurGUIStringInternal_vtable`, from Lane's Ghidra DB) before reading the `c_string` at `+0x14`. Garbage values fail the vtable check and we return `false` instead of dereferencing. SEH wrap kept as defense for the rare case where `guiString` itself points at unmapped memory.
- Cost: one extra 4-byte read per `ReadGuiString` call when `guiString` is non-null. No syscall, no `VirtualQuery`.

**Artifacts:**
- Pre-fix log: `<game install>/logs/patch-20260503-162139.log`
- Verification log: `<game install>/logs/patch-20260503-170800.log`
- Crash dump: `C:\Users\fabia\AppData\Local\CrashDumps\swkotor.exe.14140.dmp`
- Disassembly snapshot used for analysis: `build/acc_dll.disasm.txt` (regenerate with `dumpbin /DISASM patches/.../accessibility.dll` if rebuilt — the file is overwritten by each run)

---

## Next session: where to start

Continue Phase 0:

- Open a fresh session, claim "Phase 0 lay-off 3", extract `engine_panels.{h,cpp}` per the spec under "Still to extract" above. Targets: the `PanelKind` enum + `kPanelKindOffsets[]` table, `PanelKindName`, `ResolveGuiInGame`, `IdentifyPanel`, `IsPanelKindInGameMenu`, and the panel-kind cache. These currently live in `Accessibility.cpp` after the chain machinery; the extraction is mechanical (no behavior change).
- Update this file at session end with the new state.
