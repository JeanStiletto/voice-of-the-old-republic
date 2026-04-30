# Accessibility Implementation — Investigation

Working document tracking what we know about modding KotOR 1 for screen-reader accessibility, what we've decided, and what's still open. Update as decisions are made or assumptions invalidated.

## Goal

Enable a fully blind player to play **Star Wars: Knights of the Old Republic 1** (Steam, v1.0.3) using NVDA / JAWS / Narrator. At minimum the player must be able to:

- Hear the name and key state of whatever the camera/cursor is currently on (creature, door, item, placeable)
- Hear all displayed dialog text and floating combat text
- Navigate menus with keyboard alone, with each focused element announced
- Hear quest log / inventory / character sheet content read linearly

## Engine summary

KotOR 1 runs on BioWare's **Odyssey Engine**, a derivative of the Aurora Engine (also Neverwinter Nights). 32-bit Windows binary. No native accessibility API. Mod surface:

- `Override\` folder (drop-in resource overrides)
- `dialog.tlk` (master string table)
- 2DA / GFF data files (extractable from BIF archives via `chitin.key`)
- NWScript (compiled to `.ncs`) — runs on every dialog node, area enter/exit, combat round, trigger
- Process memory (closed-source binary, but heavily reverse-engineered by the community)

## Reverse-engineering and tooling landscape (community)

Two upstream projects from **Lane Dibello** dominate what's practical to build on:

### KotorPatchManager (in-process)

Runtime DLL-injection patching framework. `KotorPatcher.dll` is loaded into `swkotor.exe` at launch by `KPatchLauncher.exe`. Patches are `.kpatch` files containing a manifest, `hooks.toml`, and compiled C++.

Hook types:
- `simple` — fixed-length byte replacement
- `replace` — longer trampoline with allocated executable memory
- `detour` — full function hook with register save / parameter prep / **call back into existing in-game functions**
- `static` — (not yet investigated)

Address resolution is per-version via SQLite `AddressDatabases/`. Each patch's `manifest.toml` declares supported game versions by SHA-256 of the EXE. **The `ScriptExtender` example patch supports `kotor1_gog_103` and `kotor1_cdcrack_103` — Steam is not currently listed.** See blocker below.

Repo: `https://github.com/LaneDibello/Kotor-Patch-Manager` (Beta 0.4.2). Cloned to `third_party/`.

### KotorMessageInjector (out-of-process)

.NET library that attaches to a running `swkotor.exe` from a separate process. Reverse-engineered the engine's `CSWMessage` event schema; can both **read game state** and **inject events**.

Key capabilities relevant to accessibility:
- `KotorHelpers.getGameVersion(out isSteam)` — recognizes Steam at this layer
- `KotorHelpers.getPlayerServerID(handle)` / `getPlayerClientID(handle)`
- `KotorHelpers.getLookingAtServerID(handle)` / `getLookingAtClientID(handle)`
- `GAME_OBJECT_TYPES` enum (creature, door, etc.)
- Generic `WriteProcessMemory` / `VirtualAllocEx` wrappers via `ProcessAPI`
- Constructs and injects `CSWMessage` packets (allocates remote memory, writes asm shim, spawns remote thread to call the engine's send-message function)

Repo: `https://github.com/LaneDibello/KotorMessageInjector` (last update June 2025). Not yet cloned.

## Steam compatibility — resolved

Our Steam `swkotor.exe` SHA-256:
`34e6d971c034222a417995d8e1e8fdd9f8781795c9c289bd86c499a439f34c88`

This is the **canonical Steam build** that Lane's framework already supports as `kotor1_steam_103`. Evidence in the cloned repo:

- `src/KPatchCore/Detectors/GameDetector.cs` has this exact SHA hardcoded as KotOR 1, v1.0.3, `Distribution.Steam`
- `docs/MULTI_VERSION_ARCHITECTURE.md` line 209 enumerates it as a known version
- 9 of the 15 example patches list it in their `supported_versions` (`AppearanceBodyVariations`, `DialogueRepliesPast9`, `SemiTransparentLetterbox`, `RepeatingBlaster`, `Widescreen`, `ShipBuildFalse`, `LevelUpLimit`, `PlanetsLimit`, plus the `4GBPatch` which uses a different Steam build's hash)
- The `ScriptExtender` example patch's manifest is the exception — it only lists GoG and cdcrack. That's a per-patch oversight, not a framework limitation. We can either edit our local copy to add Steam, or just write our own patch that declares Steam from day one.

The Patch Manager also handles **Steam DRM specifically**: Steam encrypts the EXE on disk, so early-bind injection fails on invalid PE headers. The launcher detects Steam distributions and switches to delayed injection — launch the game normally, wait for Steam to decrypt, validate the real game process via PE header MZ signature at `0x400000` (filtering out Steam's bootstrap stub), then inject `KotorPatcher.dll` into the live process. Implementation in `src/KPatchCore/Launcher/ProcessInjector.cs:LaunchSteamWithInjection`. We get this for free.

The implication is that earlier in this investigation we treated the Steam-vs-GoG SHA mismatch as a real blocker requiring a fallback architecture. That was wrong — the conclusion came from reading only the `ScriptExtender` patch's manifest and not the framework as a whole.

## Architecture decisions

### Decision 1: Implementation = `.kpatch` via Lane's Patch Manager (in-process DLL injection)

**Rationale:**
- Steam is already a supported version; no fallback architecture needed
- In-process hooks give us *event-driven* accessibility (text appears → our hook fires → we speak it) instead of polling, which is decisively better for: dialog text, floating combat numbers, menu focus changes, instant HUD updates
- One launcher (KPatchLauncher), one user-facing binary, no companion process
- Detour hooks let our code call existing in-game functions — we can re-use the engine's own name-resolve / string-fetch routines instead of reverse-engineering them ourselves
- Distributable through Lane's roadmap toward HoloPatcher (TSLPatcher's modern replacement)
- Steam DRM injection is already solved by the launcher

**Cost:**
- C++ DLL development is harder than C# in-process polling
- Crashes in the patch crash the game with it (mitigated by good engineering, not architecture)
- Build chain: requires Visual Studio 2022 (C++ desktop + .NET workloads). Not yet installed.

### Decision 2: Companion app (KotorMessageInjector) demoted to "research / debug aid"

`KotorMessageInjector` stays useful as a quick-iteration research tool — attach to the live game, dump state, prototype which fields are interesting before committing them to a hook. But it is **not** the delivery channel for the accessibility mod. We use the in-process patch instead.

### Decision 3: Distribution will follow Lane's roadmap toward HoloPatcher

Lane explicitly plans HoloPatcher (`OldRepublicDevs/PyKotor`) integration with `.kpatch` files declarable in `changes.ini`. We align with this so accessibility ships through the same pipeline KotOR mod users already know.

## Findings to date (chronological)

- 2026-04-29: Steam KotOR 1 install located, SHA-256 captured.
- 2026-04-29: DeadlyStream thread `kotor-1-gog-reverse-engineering` identified Lane's RE Ghidra archive (Google Drive) and the Patch Manager GitHub repo.
- 2026-04-29: Patch Manager cloned. Confirmed it is a runtime DLL-injection framework. Initial check of the `ScriptExtender` patch manifest showed only GoG/cdcrack support — *misread as a framework-wide limitation*.
- 2026-04-29: Discovered `KotorMessageInjector` — Lane's external-process tool. Includes Steam recognition and "what is the player looking at" helpers. `Adapter.GetClientObjectName()` can resolve a clientId to a human-readable name.
- 2026-04-29: JDK 21 + Ghidra 12.0.4 installed under `C:\Tools\` per `docs/tools.md`. `JAVA_HOME` set at user level. Ghidra GUI smoke test still pending.
- 2026-04-29: `KotorMessageInjector` cloned to `third_party/`. `testApp/Program.cs` reviewed — confirms the API surface and reveals `Adapter` class with high-level helpers.
- 2026-04-29: **Steam compatibility re-evaluated.** A grep across the cloned Patch Manager shows our Steam SHA is hardcoded in `GameDetector.cs` as the canonical `kotor1_steam_103`, listed in 9 example patches' `supported_versions`, and documented in `MULTI_VERSION_ARCHITECTURE.md`. The launcher has a Steam-specific delayed-injection path that handles Steam DRM PE encryption. The earlier "SHA mismatch" reading was based on inspecting only one patch and was wrong.
- 2026-04-29: **Architecture decision reversed.** The in-process `.kpatch` path is now the primary plan. Companion-app via MessageInjector demoted to research / debug tool.
- 2026-04-29: **Lane's full RE bundle pulled.** All 5 files from his Google Drive folder downloaded into `docs/llm-docs/re/`: `k1_win_gog_swkotor.exe.gzf` (28 MB, imported into a Ghidra project at `C:\Tools\ghidra-projects\kotor1`), `.xml` (31 MB Program XML), `.sarif` (490 MB SARIF analysis export — full vtable layouts, comments, xrefs, datatypes), `.h` (745 KB Ghidra C header), `KotOR 1 System Layout-2.pdf` (28 MB architecture doc). Independent confirmation that GoG-derived Ghidra bytes match Steam at common code addresses (verified by cross-checking `0x00552c9a`).
- 2026-04-29: **Headless RE toolchain established.** `tools/ghidra-scripts/DumpBytes.java` and `Decompile.java` invoked via `analyzeHeadless.bat -process k1_win_gog_swkotor.exe -readOnly`. SARIF queryable via `jq` (installed at `C:\Users\fabia\bin\jq.exe`); recipes in `docs/llm-docs/sarif-cookbook.md`. Address DB queryable via `sqlite3` directly. Three independent windows into Lane's RE — pick by query type, all ~5–10 sec per query.
- 2026-04-30: **Step 6 complete.** Two detour hooks installed and producing structured engine events. `HandleInputEvent` decodes key codes via the 132-entry `InputIndices` enum lifted from the SARIF — log entries look like `HandleInputEvent #N this=07492078 key=KEYBOARD_L(62) val=1`. `HandleFocusChange` confirmed not to fire on the main menu (consistent with the menu's specialized `HandleInputEvent` dispatch path) but fires elsewhere. Pivot during this session: started with entry-point hooks + `source = "esp+X"` stack params (got addresses instead of values, see next entry); pivoted to mid-function hooks with register sources; both work correctly.
- 2026-04-30: **Upstream framework bug discovered and sidestepped.** `Kotor-Patch-Manager`'s wrapper at `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp:340-361` emits `LEA ECX, [ESP+offset]; PUSH ECX` for `source = "esp+X"` declarations — pushes the address rather than the value. The "Fix stack offset wrapper" commit (`ced6249`, Jan 2026) only updated the comments to match the buggy code, not the code. Confirmed by reading the wrapper source and verifying empirically (got consecutive 4-byte-apart stack addresses where `InputIndices` values were expected). Our hooks now use mid-function + register-source pattern exclusively; PR opportunity captured in `memory/project_kpatchmanager_lea_bug.md` for the community.
- 2026-04-30: **Step 7 complete — Tolk wired up, screen reader speaks.** Vendored upstream `Tolk.dll` + `nvdaControllerClient32.dll` (x86) under `third_party/tolk/x86/` along with LGPL license texts. New `tolk.{h,cpp}` module loads Tolk via `LoadLibrary`/`GetProcAddress` (graceful degradation if Tolk is missing — patch DLL still loads, hooks still log). DLL search path is briefly redirected via `SetDllDirectoryA(<patches dir>)` around `Tolk_Load()` so the NVDA driver client DLL resolves, then restored. **Tolk init is lazy on first hook fire — not in DllMain** — because Tolk loads driver DLLs and initializes COM, both unsafe under the loader lock. Verified: `Tolk: loaded, screen reader = NVDA`, then a startup announcement spoken aloud.
- 2026-04-30: **Project layered into log.* / tolk.* / Accessibility.cpp.** `Accessibility.cpp` is now just DllMain + `OnXxx` detour entry points; logging primitives extracted to `log.{h,cpp}` (namespace `acclog`); Tolk wrapper at `tolk.{h,cpp}` (namespace `tolk`). `kdev apply` extended to copy `Tolk.dll` + `nvdaControllerClient32.dll` from `third_party/tolk/x86/` into `<install>/patches/` alongside our patch DLL after `PatchApplicator.InstallPatches` runs. Build script (`create-patch.bat`) globs `*.cpp` so multi-file builds work without further changes.
- 2026-04-30: **Earlier "main menu uses HandleInputEvent, not the focus path" theory was wrong.** `CSWGuiMainMenu::HandleInputEvent` at `0x67b395` only handles letter / F-key shortcuts — arrow-key navigation does NOT reach it. The real focus-change entry point is `CSWGuiPanel::SetActiveControl` at `0x0040a630` (`void __thiscall(CSWGuiControl* param_1)`). It fires once per actual focus change, covers main menu + every sub-screen, and gives the new control as `param_1` (or NULL on deactivation). Hooked mid-function at `0x0040a638` with EDI=panel, ESI=newControl. Old memory `project_main_menu_input_path.md` updated in place with the corrected guidance.
- 2026-04-30: **Vtable downcast pattern verified for label extraction.** From the `OnSetActiveControl` handler we call `vtable[22](control)` (`AsButton`) and `vtable[20](control)` (`AsLabel`) as `__thiscall`. Each returns `this` cast to the subclass or NULL. Trivial implementations (no engine state mutation, no allocation) → safe to call from inside a hook. With concrete subclass pointer in hand we read the CExoString at known offsets: `+0x16c` for `CSWGuiButton::text.text_params.text`, `+0xe8` for `CSWGuiLabel::text.text_params.text`. German Steam build verified: cycling through main-menu items produced clean localized labels (`"Neues Spiel"`, `"Spiel laden"`, `"Filmsequenzen"`, `"Optionen"`, `"Beenden"`); navigating into Options switched the panel pointer transparently and announced `"Gameplay"`, `"Feedback"`. Concrete offsets + vtable indices captured in `memory/project_kotor_gui_struct_offsets.md`.
- 2026-04-30: **Three failure modes identified for character-creation announcements.** **(A)** "Correct announcement" — control is a button or label, vtable downcast succeeds, real text spoken. **(B)** `"control N"` placeholder — `AsButton` and `AsLabel` both return NULL, so we fall back to the integer ID. The control is something else: most likely `CSWGuiListBox` (race / class / portrait picker), `CSWGuiSlider` (attribute points), `CSWGuiEditbox` (name field), or `CSWGuiButtonToggle`. Need additional vtable indices (21=LabelHighlight, 23=ButtonToggle) plus RE for ListBox / Slider / EditBox struct layouts. **(C)** "Nothing" — `SetActiveControl` doesn't fire at all. Probable cause: focus moves *within* a child element (e.g., a row of a listbox) that the parent listbox manages internally, never going through `CSWGuiPanel::SetActiveControl`. Likely fix: also hook `CSWGuiListBox::SetActiveControl` at `0x0041c160` (different signature: takes 2 params).
- 2026-04-30: **Step 8.1 done — extended downcasts + always-on diagnostic logging.** `ExtractAnnounceableText` now also tries `AsButtonToggle` (vtable 23) and `AsLabelHighlight` (vtable 21) — both reuse the existing `+0x16c` / `+0xe8` text offsets because `CSWGuiButtonToggle` and `CSWGuiLabelHilight` embed their parent (`CSWGuiButton` / `CSWGuiLabel`) at offset 0. Logging policy split: resolved focus events stay rate-limited (noisy on idle navigation), but **`src=none` events are always logged** with the control's vtable pointer + a few entries (`vtable=ADDR [0]=... [4]=... [20]=... [22]=...`). Mapping those vtable addresses to subclasses via SARIF is the next-session bootstrap for adding Slider / Editbox / ListBox extraction safely (no speculative reads, since reading at `+0x158` on a 320-byte object risks AV).
- 2026-04-30: **Step 8.2 first half — `CSWGuiListBox::SetActiveControl` hooked at `0x0041c16b`.** Listbox row navigation never bubbles to `CSWGuiPanel::SetActiveControl` — the listbox runs its own internal dispatcher. New `OnListBoxSetActiveControl(this, param_1, param_2)` handler runs `ExtractAnnounceableText` on the row, falls back to `"row N"` placeholder, and always-logs unknowns with vtable. Hook bytes from `DumpBytes.java`: 5-byte cut wasn't aligned to instruction boundaries, so used 8-byte cut at offset `+0x0b` (`50 51 8d 8e 9c 02 00 00` = PUSH EAX + PUSH ECX + complete LEA), where ESI=this, ECX=param_1, EAX=param_2. Build verified clean. **Pending live verification** in chargen / save-game listbox.

- 2026-04-30: **Live session 3 — chargen + Options reachable; major issues mapped.** Confirmed working: main menu, Options tabs, chargen entry buttons (Standardcharakter / Eigener Charakter + their help text), Portrait / Attribute entry, attribute value buttons (slider value labels read as `text="8"`/`text="10"`/`text="12"`), confirmation popups (OK / Abbrechen), error dialogs ("Eigenschaftswerte können nicht unter 8 Punkte gesenkt werden"). Log: `<install>/logs/patch-20260430-014207.log`. Outstanding issues, in order of impact:
  1. **TLK lookup re-disabled.** Session 2 `LookupTlk` (calling `CTlkTable::GetSimpleString` via `__thiscall` from inside our hook) caused the patch to go silent partway through Options, then NVDA stayed mute the rest of the session. No crash — game ran fine — but our hook log stopped at the first `src=none` event. Reverted to a logging-only stub. **Hypothesis:** the engine call needs an SEH guard or we have a struct-size mismatch on `CExoString`. Re-enabling is blocked on understanding the failure mode (try `__try`/`__except` around the call first).
  2. **Listbox-as-single-blob.** The Options Gameplay panel exposes a single `CSWGuiListBox` whose `row` is a `CSWGuiLabel` whose CExoString contains all 8 menu titles concatenated by newlines (`"Kampf-Schwierigkeitsgrad\nAutom. Levelaufstieg\n..."`). NVDA reads the whole thing as one utterance. The user can't tell which item is "current". Likely the engine renders each line as a separate visual row but the underlying control aggregates. **Likely fix:** read `CSWGuiListBox.selection_index` (offset `+0x2c6`) to know which row is active, plus walk `controls` (CExoArrayList at `+0x29c`) to fetch the individual items — *or* split the aggregated string by newline and announce only the line at `selection_index`.
  3. **Two vtables still produce `src=none` placeholders, identified but not extracted:**
     - `vtable=0073E658` — destructor at `0x00419E20` is `CSWGuiButton::~CSWGuiButton`. Plain `CSWGuiButton` instances appearing as id=11/12/13 in the Options panel `0764D1C8`. AsButton returns self, +0x16c CExoString is empty, str_ref likely populated.
     - `vtable=0073E5B8` — destructor at `0x00419D80` is `CSWGuiLabel::~CSWGuiLabel`. Plain `CSWGuiLabel` listbox-row content (`row=1210569C`) in chargen attribute panel; AsLabel returns self, +0xe8 CExoString is empty, str_ref likely populated.
     - These are exactly the cases TLK lookup was meant to resolve. Once issue #1 is fixed, both should announce real text.
  4. **Input-event hook is title-screen-only.** `CSWGuiMainMenu::HandleInputEvent` at `0x0067b395` only fires on the title screen. Once the user enters Options or chargen, key events are dispatched by per-screen `HandleInputEvent` overrides we don't hook — so we have zero visibility into arrow-key presses from the Options panel onward. The user reported arrow keys "don't seem to work" in sliders (game prompt says use arrows, but arrows produce no audible response) and "only one option is navigable with arrows" in Settings. We can't currently tell from the log whether the arrows are reaching the engine or being eaten somewhere — **we need a per-class HandleInputEvent hook per relevant screen, OR a single hook on the base `CSWGuiPanel::HandleInputEvent`** if it exists. Investigate whether there's a virtual-dispatch entry point we can hook once and catch every screen.
  5. **Repeated `row 0` placeholder on stable hover.** When the user is sitting on an empty-help-text row in chargen, the listbox dispatcher fires repeatedly with the same row pointer and id=0. With "fallback always speaks" (queued), NVDA queues "row 0" over and over. **Likely fix:** dedup the placeholder *only* against the newControl pointer (not the spoken text), so a stable focus on an unknown control announces once but a navigation onto a different unknown control still announces.

- 2026-04-30: **Speech policy locked in.** Use `interrupt=false` (queued) for all speech — `interrupt=true` cascades caused full silence in chargen when 5–10 focus events fired in <1s. Generic placeholders ("control N", "row N") bypass dedup entirely (memory: `feedback_never_silence_fallback_announcement.md`); successful announcements use per-channel dedup (`SpeakIfChanged`) to suppress per-frame listbox spam.

- 2026-04-30: **Live session 4 — Settings menu diagnosed; path-C work begins.** Three diagnostic features deployed and validated against the Options screen, exposing the architecture of the listbox-as-blob and the broken tab-cycle.
  1. **Manager-level input hook.** Replaced the title-screen-only `CSWGuiMainMenu::HandleInputEvent` (`0x67b395`) with `CSWGuiManager::HandleInputEvent` at `0x0040c907` (6-byte mid-function cut, sources ECX/EBX/EAX = this/param_1/param_2). One hook, every screen — captures press AND release edges (we land *before* the `param_2 == 0` JZ this time, val=128 = press, val=0 = release). Old issue #4 ("input-event hook is title-screen-only") resolved.
  2. **Manager-level codes need translation.** First post-deploy log showed `?(181)`, `?(182)`, …, `?(223)` — codes outside our 132-entry `InputIndices` table. Decompiling `0x0040c8e0` revealed two inline switch statements that translate KOTOR-internal "logical action" codes (`0xb4-0xb9`, `0xbb`, `0xdf`) into InputIndices values *after* our hook fires. Mirrored the table into `ManagerTranslateCode()` in `Accessibility.cpp`; logs now print `key=logical(183) -> KEYBOARD_X(0x3e) val=128`. Codes outside the translation set pass through unchanged — at the manager level we see two tiers: pre-translation logical codes for nav/system keys, and already-InputIndices codes for everything else.
  3. **Panel + listbox child walks.** `OnSetActiveControl` and `OnListBoxSetActiveControl` now log a one-shot `Panel walk parent=… children=N` / `ListBox walk parent=…` dump on first focus into a previously-unseen container, walking `controls` (`CExoArrayList<CSWGuiControl*>`) at `+0x20` for panels and `+0x29c` for listboxes. CExoArrayList layout `{ T** data; int size; int capacity; }` confirmed via SARIF DATATYPE entries. Every listbox event additionally logs `ListBox::cursor list=… sel=… top=… perPage=…` so we can see the engine's internal selection state move (or not).
  4. **Settings menu architecture revealed.** Reference log `<install>/logs/patch-20260430-070010.log`. Findings:
     - **Options panel has 5 tabs but engine cycles only 2.** `Panel walk` for `074D6E88` reports 9 children — `[1] Gameplay`, `[2] Auto-Pause`, `[3] Grafik`, `[4] Sound`, `[5] Feedback` are all fully-extractable buttons, plus the listbox at `[0]`, `[6] Optionen` header label, `[8] Schliess.` button. But arrow-key navigation only ever reaches `[1] Gameplay` and `[5] Feedback`. The engine's tab-cycle order skips `[2]/[3]/[4]` entirely — likely a missing focus-traversal bit on those controls. This is what the user feels as "only one item navigable" — it's literally a 2-step cycle on a 5-tab panel.
     - **Settings listbox is one multi-line label, not 8 rows.** `ListBox walk parent=074D7DC8 children=1` — the listbox holds a single `CSWGuiLabel` containing all 8 setting names joined by `\n`. `ListBox::cursor sel=-1 top=0 perPage=1` constantly: there is no engine-side row cursor, because there are no engine-side rows. The previous "read `selection_index` and announce row N" plan won't work here. Path forward: split the multi-line CExoString on `\n` and render keyboard navigation synthetically with our own cursor. Mouse clicks on individual visual lines presumably trigger settings-change behavior via Y-coordinate hit-testing inside the listbox — but the click → action path is **not yet RE'd**. Diagnostic logging to capture this is the next session's first task.
  5. **All log rate limits removed.** Per `feedback_log_no_rate_limits.md`: every `acclog::Write` in every handler now runs unconditionally. Logs are larger but lossless — Claude consumes them whole, sampling hides load-bearing single events (one `selection_index` jump, one new `vtable=`, etc.). `SpeakIfChanged` dedup remains; that's a speech rule, not a logging rule.
  6. **Old issue inventory updated.** From the 2026-04-30 (session 3) entry's five issues: #4 (per-screen input) is **resolved**. #2 (listbox-as-blob) is **empirically confirmed and the previously-suggested `selection_index` approach is invalidated** — the settings listbox sits at `selection_index = -1` permanently because it has no rows in the structural sense. The replacement plan is per-newline split + synthetic cursor. Issues #1 (TLK lookup), #3 (`vtable=0073E658` / `0073E5B8` plain-Button/Label content empty), #5 (`row 0` spam dedup) all still open.

## Next steps

Ordered. Each step has a definite "done" condition. Steps marked ✅ are complete.

> **Resume here next session:** session 4 done — manager-level input hook, panel-children walk, listbox-children walk + cursor read, manager-translation table all live. Settings menu architecture mapped: 5 tabs (3 unreachable), listbox-as-blob confirmed. Goal for next session is **path C** (modal nav with arrow takeover + click synthesis) — the only path that lets a blind user actually *change* settings, not just hear them. Path C requires understanding how mouse clicks on the listbox translate into setting-change actions, which is currently a black box. Tackle in priority order:
>
> 1. **Click-path diagnostics.** Add hook coverage for the engine functions that handle mouse clicks on listbox lines + the resulting setting-change calls. Specifically: find and hook the mouse-click handler on `CSWGuiListBox` (likely something like `OnMouseClick` / `HandleMouseEvent` / `HitTest` per-class — query SARIF), and any callbacks the listbox fires into the parent panel when a line is clicked. Also log mouse coordinates as they arrive at the manager. *Done when:* with NVDA off and patch logging on, the user clicks a setting line with the mouse and the log shows the full chain — mouse coords → listbox hit-test → callback dispatch → setting-change function. We need that chain to know whether path C is "synthesize a mouse click at line Y" or "call the setting-change function directly".
> 2. **Synthetic per-line announcement (path A).** Independent of click synthesis: when a listbox is detected as multi-line-label-blob (`children == 1` and the child is a Label with `\n` in its text), split the CExoString on `\n` and queue each line as a separate Tolk utterance instead of one run-on. Pure read-only win, no input interception. Done when entering Options-Gameplay produces 8 NVDA utterances instead of one.
> 3. **Re-enable TLK lookup safely** (carries over from session 3). Wrap `CTlkTable::GetSimpleString` in `__try`/`__except`, bounds-check strref. Resolves `vtable=0073E5B8` Label and `vtable=0073E658` Button `src=none` cases.
> 4. **Pointer-based dedup for placeholders** (`row 0` spam fix from session 3). Track last-seen `newControl*` per channel; suppress repeats only when the pointer matches. Lower priority now that rate limits are off — `row 0` spam shows up in the log but doesn't hurt the user since `SpeakIfChanged` already dedups identical strings.
> 5. **Investigate why three Options tabs are skipped** (Auto-Pause / Grafik / Sound). Probably a per-control flag in `CSWGuiButton` or a tab-cycle list on `CSWGuiPanel`. SARIF query for fields with names like `is_navigable`, `tab_stop`, `cycle_order` on the Button/Panel datatypes. If it's a single bit, fixing it might be cheaper than synthesizing tab navigation ourselves.
>
> Use `dbgcapture` (in `tools/dbgcapture/`) for any future KotorPatcher diagnostics; the patch's own log file is `<install>\logs\patch-<utc>.log`. Latest reference log: `patch-20260430-070010.log`.

1. ~~**Smoke-test Ghidra.** Launch `C:\Tools\ghidra_12.0.4_PUBLIC\ghidraRun.bat` once.~~ ✅ Done 2026-04-29 — Ghidra launched cleanly with JDK 21.
2. **Validate Steam compatibility end-to-end.** Download the Patch Manager Beta 0.4.2 release zip (no compile needed). Run `KPatchLauncher.exe`, point it at our Steam `swkotor.exe`, apply a patch that already declares Steam support (e.g. `Widescreen` or `RepeatingBlaster`), and click Launch. *Done when:* the game starts with the patch active. *If it fails:* capture the launcher's log and diagnose. Most likely causes: antivirus interference, missing VC++ runtime, Steam launching its own bootstrap and confusing detection.
3. **Read three docs in order:** `third_party/Kotor-Patch-Manager/docs/MULTI_VERSION_ARCHITECTURE.md`, `docs/AddressDatabaseSystem.md`, then `Patches/ScriptExtender/` source (`manifest.toml`, `hooks.toml`, `ScriptExtender.cpp`, plus the `additional/nwscript.nss`). *Done when:* we can describe how addresses are resolved per-version, and how a patch exposes a new NWScript function.
4. ~~**Install Visual Studio 2022 Community.**~~ ✅ Done 2026-04-29 — installed **Build Tools 2022** instead (no IDE, smaller). `cl.exe` 19.44.35226 for x86 verified working via `vcvars32.bat`. Path: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\`. See `docs/tools.md`.
5. ~~**Build a do-nothing accessibility patch.**~~ ✅ Done 2026-04-29 — full chain verified end-to-end on Steam.

   - **Patch source** in `patches/Accessibility/{manifest.toml, hooks.toml, exports.def, Accessibility.cpp}`. Manifest declares `kotor1_steam_103` + `kotor1_gog_103`. Hook is one detour at `0x00552c9a` (`CSWRules::CSWRules` constructor — address + 10-byte signature borrowed from upstream `LevelUpLimit`, the Steam-supported reference patch). `skip_original_bytes = false` so the engine's own init runs after our handler.
   - **Handler `OnRulesInit`** is one-shot via `static bool fired`; logs once. `DllMain` logs once on `DLL_PROCESS_ATTACH`. Per-launch log file: `<install>\logs\patch-<utc>.log`. Path resolved from `GetModuleFileNameA(hinstDLL)` so it does not depend on CWD. Every `WriteLog` line is also mirrored to `OutputDebugStringA` for `dbgcapture` to catch.
   - **Critical bug found and worked around: KPatchCore's in-process injection assumes launcher arch == target arch.** Its `ProcessInjector.cs` reads `kernel32.dll`'s address from the calling process and reuses it inside the game's address space. KOTOR is 32-bit (WoW64); a 64-bit kdev had its kernel32 mapped at 0x7FFA…, an address invalid in the WoW64 game. `CreateRemoteThread` reported success because the thread *terminated* (by crashing), not because it ran `LoadLibraryA`. Workaround: `kdev launch` now shells out to the prebuilt `KPatchLauncher.exe` (i386), which has the correct kernel32 address. Symptom if this regresses: `[Injector] kernel32.dll handle: 0x7FFA…` in the launch output (anything in the high range is wrong). Correct: `0x74…` or similar low-32-bit address.
   - **`tools/dbgcapture/`** built — minimal C# console app that listens on the global `DBWIN_BUFFER` shared section + `DBWIN_BUFFER_READY`/`DBWIN_DATA_READY` events and writes every captured `OutputDebugStringA` to a file. Replaces DebugView for our use, no admin / GUI / driver. Run `tools/dbgcapture/bin/Debug/net10.0/win-x64/dbgcapture.exe <log_path> --quiet` before any debug session.
   - **Verified output (2026-04-29 19:33Z, PID 32808):** marker file written, `<install>\logs\patch-20260429-193323.log` contains both expected lines, `dbgcapture.log` shows the full KotorPatcher trace including `Applied DETOUR hook at 0x00552C9A -> OnRulesInit`. **Steam's bytes at `0x00552c9a` match the GoG signature** — our assumption that the address DB's GoG values work for Steam holds for this address. Not a guarantee for arbitrary other addresses; verify each.
6. ~~**First useful detour: capture engine events.**~~ ✅ Done 2026-04-30 — two detour hooks installed, both capturing structured events.

   - **`CSWGuiMainMenu::HandleInputEvent`** detoured at `0x0067b395` (mid-function, after the engine has loaded `param_1` into EBX, `param_2` into EDI, `this` into ESI). Handler `OnHandleInputEvent` decodes `param_1` against the `InputIndices` enum (132 entries lifted from the SARIF; full table in `Accessibility.cpp`) and logs the named key. Filters key-release events automatically: we land *after* the engine's `if (param_2 != 0)` early-out so only press-side events reach us.
   - **`CSWGuiControl::HandleFocusChange`** detoured at `0x0041896b`. Handler `OnHandleFocusChange` logs `this` and `param_1` (focus state). Confirmed not to fire on the main menu — the title screen routes input through `HandleInputEvent` exclusively (see `memory/project_main_menu_input_path.md`). Fires on other GUI surfaces; full validation pending in-game work.
   - **Hook design pattern locked in:** mid-function + register sources. The framework's stack-source path (`source = "esp+X"`) is broken upstream (LEA-vs-MOV bug — emits address rather than value). Captured in `memory/feedback_hook_design_register_sources.md` and `memory/project_kpatchmanager_lea_bug.md` so future hooks default correctly. Specifically, when authoring a new hook: read the prologue with `tools/ghidra-scripts/DumpBytes.java`, find the point where args have been moved into named registers, hook there with `source = "ebx"` / `"edi"` / etc.
   - **Verified output (2026-04-30):** real keys decoded — e.g. `HandleInputEvent #N this=07492078 key=KEYBOARD_L(62) val=1`, `HandleInputEvent #N+1 this=07492078 key=KEYBOARD_K(61) val=1`, `HandleFocusChange #1 this=074561E8 p1=1`. Idle / mouse-move noise filtered for free by the early-out.
   - **Original step 6 ("capture displayed text") deferred to step 8.** That capture style — hooking a string-display function and logging the rendered text — is still on the roadmap but is not the first thing we'll need; the input-event capture above is what makes the title screen / chargen navigable in the first place. Dialog / HUD text capture lands once we have an in-game test surface to validate against.
7. ~~**Wire to a screen reader.**~~ ✅ Done 2026-04-30. Tolk loaded via `LoadLibrary` from `third_party/tolk/x86/Tolk.dll`. Lazy init on first hook fire (not in DllMain — Tolk loads COM + driver DLLs, unsafe under loader lock). `SetDllDirectoryA` redirected briefly around `Tolk_Load` so `nvdaControllerClient32.dll` (also bundled under `<install>/patches/`) resolves. NVDA detection verified; startup announcement spoken; per-focus announcements working with real labels.

8. **Expand hook coverage.** Concrete sub-steps:

   1. ~~**More vtable downcasts** — add `AsButtonToggle` (vtable 23) and `AsLabelHighlight` (21) to `ExtractAnnounceableText`'s lookup chain.~~ ✅ Done 2026-04-30 (session 3). Diagnostic logging is also fully unthrottled now (session 4) per `feedback_log_no_rate_limits.md`.
   2. **Listbox / Slider / Editbox.** Listbox `SetActiveControl` hook at `0x0041c16b` ✅ done (session 3, validated session 4). Listbox `controls` walk + `selection_index` read ✅ done (session 4). Slider / Editbox subclass-aware extraction still pending — the chargen attribute panel exposes value-buttons that read correctly, but real Slider widgets (e.g. Options→Sound→volume) are untested.
   3. **Dialog text** — hook the engine's dialog-line display function. Per the SARIF, look for functions in `CSWGuiInGameDialog` or similar. Done when in-game conversations are spoken aloud.
   4. **Floating combat text** — hook the function that renders damage / status numbers above creatures. Suppress redundant repeats (NVDA queueing 30 "1 damage" lines is useless).
   5. **Inventory / character sheet / journal** — these are CSWGuiPanel-based, so `SetActiveControl` already fires; the work is mostly subclass coverage from sub-step 2 + announcing item *details* (description, stats) when an item gets focus.
   6. **Game state transitions** — area enter/leave, level-up, save/load. Likely surfaced via NWScript hooks or specific engine functions.

9. **HoloPatcher distribution.** Once the patch is functional, follow Lane's roadmap to declare it in `changes.ini` so HoloPatcher can install it like any other KotOR mod.

## Open questions

- **Will the Patch Manager release zip Just Work against our Steam install?** Resolved by step 2. Most likely yes given the framework's Steam detection + delayed-injection path is already implemented.
- **What does the Patch Manager's `static` hook type do?** Mentioned in the README's hook-type list but not described in the section we read. Skim `KPatchCore` to find out.
- **Which engine functions should we detour for accessibility coverage?** Specifically for: dialog text display, floating combat text, menu/HUD focus changes, area transitions, status/quest updates. Identify via Ghidra after step 3 / step 6 work begins.
- **How does the Patch Manager expose new NWScript functions?** The `ScriptExtender` patch is the working example; its source plus its `additional/nwscript.nss` will show the mechanism. Read in step 3.
- **What screen-reader bridge to use from inside a C++ DLL?** Tolk is C, so it's directly callable from C++. Confirm Tolk works when called from within an injected DLL (likely fine — it just calls into the screen reader's IPC). NVDA Controller Client is a fallback for NVDA-only users.
- **Will Tolk + an injected DLL trigger antivirus?** Many AVs flag any program that does process injection or remote thread spawning. The Patch Manager already does this for every user; if it works in practice for Lane's existing patch users we should be fine.
- **License posture of Patch Manager.** No `LICENSE` file in the repo. Treat as "all rights reserved" until Lane clarifies — relevant when we publish our derivative `.kpatch`. Worth asking Lane directly given his roadmap explicitly invites collaborators.
- **Fullscreen exclusive vs windowed.** Screen readers can interact poorly with exclusive fullscreen. May need to recommend (or even force) borderless-windowed in the README.
- **What happens when the user dies / area-transitions / loads a save?** Hooks must handle game-state transitions cleanly. Likely needs an `OnGameStart` / `OnLoadGame` notification we can attach to.
- **Build artifact location and project layout for our own `.kpatch`.** Should it live as a fork-folder inside `third_party/Kotor-Patch-Manager/Patches/Accessibility/`, or as our own repo + a build step that places it into the patches folder for testing? Decide before step 5 to avoid rework.
