# Upstream PR Opportunities

Tracks fixes and features we'd offer back to upstream projects. Each entry
captures: what, why, where, and current state.

Most upstream is `LaneDibello/Kotor-Patch-Manager`. Others added as they come up.

## Re-verification note (2026-07-21, after rebasing our vendored tree onto KPM 0.6.0)

Our vendored KPM tree was rebased from base `677a72d` onto tag `0.6.0`
(commit `ca58d1a`). Re-verified each KPM PR against the current 0.6.0 source
before trusting these briefs (they were written early and some framing is
dated):

- **PR-1 (consumed_exit_address):** present in our tree; upstream never touched
  `wrapper_x86_win32.cpp` or any PR-1 support file, so it still applies cleanly
  to master. File references below predate the rebase — line numbers shifted.
- **PR-2 (LEA-vs-MOV `esp+X`):** bug **still live** upstream and in our tree.
  The buggy emit is `LEA ECX, [ESP+offset]` (now ~lines 414/419/425 of
  `wrapper_x86_win32.cpp`), a *separate* path from the correct `MOV` (0x8B)
  param path. Before filing, re-read whether this LEA is the `type=pointer`
  path (in which case LEA is correct and the bug is narrower than documented) —
  do not file half-checked. We still avoid `esp+X` (register sources only).
- **PR-3 (selective-POPAD LEA-ESP) / PR-4 (EFLAGS PUSHFD/POPFD):** both present
  in our tree (`LEA ESP,[ESP+4]` and `PUSHFD`/`POPFD` around the consume TEST).
  Wrapper untouched upstream, so both still apply cleanly.
- **PR-5 (AllowVersionMismatch):** not upstream. 0.6.0's version gate still
  hard-fails (`ValidateAllPatchesSupported`), and `InstallOptions` gained
  `ProxyDllPath` but no bypass. Our field/branch merged in beside it. 0.6.0's
  managed-install-state (#94) is a *related but narrower* mechanism (tracks the
  clean hash after KPM's own edits; does not cover a third-party-modified exe).
- **New empirical note:** static exe patches cannot apply to the SteamStub-
  encrypted Steam exe (confirmed live — `borderless_fullscreen` byte-verify
  failed). Relevant to any static-hook PR validated only on GoG.

`docs/framework-changes-backup.patch` (referenced by old notes) was removed;
the vendored tree's `git log` is the record. Do not submit any upstream PR
without the dev's explicit go-ahead.

## Submission status (2026-07-21)

- **PR A — SUBMITTED:** LaneDibello/Kotor-Patch-Manager **#132** (selective-POPAD ESP-slot fix). Open, awaiting review.
- **PR B — SUBMITTED:** LaneDibello/Kotor-Patch-Manager **#133** (`consumed_exit_address`), stacked on #132. Open, awaiting review.
- **PR C — NOT YET:** `AllowVersionMismatch` — held back deliberately; the dev wants to check something first (a later session).
- Fork: `JeanStiletto/Kotor-Patch-Manager` (branches `pr-a`, `pr-b`). Both build-verified (C++ + C#) before submission. Disclaimer included in each PR body (well-tested downstream, but AI-assisted — review critically).

## Submission grouping (2026-07-21) — how the internal PR-1..PR-5 map to real PRs

The internal PR numbers below are our *tracking* IDs, not the shape of the
actual PRs. Code-quality reviewed and the vendored comments scrubbed of all
project-internal references (our PR numbers, doc/memory paths, task jargon like
"Phase 3 lay-off 5", accessibility/CSWGuiManager/UniWS specifics) — the tree is
now PR-ready. Three PRs, in suggested submission order:

**PR A — "Fix selective-POPAD ESP-slot handling in the detour wrapper"** (was PR-3)
- One file: `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp`.
- Skip the ESP slot in the manual pop loop (`POP ESP` corrupts the restore), and
  use `LEA ESP,[ESP+4]` instead of `ADD ESP,4` for skipped slots (flag-preserving).
- Small, self-evidently correct bugfix in *existing* code (the selective-restore
  path used when `exclude_from_restore` is non-empty). Latent upstream — no
  shipping patch exercises that path yet — so it reads as a clean latent-bug fix.
  Submit first; it stands alone.

**PR B — "Add consumed_exit_address: conditional consumed-event exit"** (was PR-1 + PR-4)
- Eight files: C# plumbing (`Models/Hook.cs`, `Parsers/HooksParser.cs`,
  `Applicators/ConfigGenerator.cs`), C++ plumbing (`include/patcher.h`,
  `include/wrappers/wrapper_base.h`, `src/config_reader.cpp`, `src/patcher.cpp`),
  and the codegen (`src/wrappers/wrapper_x86_win32.cpp`).
- Additive, default-off feature: when a handler returns non-zero, the wrapper
  jumps to a caller-specified address instead of the natural fall-through — lets
  a hook consume an event. Includes the `PUSHFD`/`POPFD` around the consume
  `TEST` (internal PR-4) as part of getting the feature right — do NOT split the
  feature from its own flag-correctness. Pairs naturally after PR A, since using
  it requires `exclude_from_restore = ["eax"]`, which is the path PR A fixes.

**PR C — "Add AllowVersionMismatch install option"** (was PR-5)
- One file: `src/KPatchCore/Applicators/PatchApplicator.cs`.
- Opt-in flag on `InstallOptions`: demotes a supported-versions hash mismatch to a
  warning; per-hook `original_bytes` verification stays the real gate. Fully
  independent of A/B — can go any time.

**Not a PR yet — the `esp+X` LEA-vs-MOV bug (was PR-2).** We don't fix it in code
(we avoid `esp+X`). Before offering it, re-read whether the `LEA ECX,[ESP+off]`
emit is actually the `type=pointer` path (where LEA is correct). File as an
*issue* with a repro, not a code PR, until that's settled.

## Active

### PR-1. Wrapper `consumed_exit_address` for conditional flow

**Repo:** `LaneDibello/Kotor-Patch-Manager`
**Status:** Designed, not yet drafted. Blocking our menu-nav work.
**Discovered:** Session 5 (2026-04-30) while designing keyboard-event consumption.

**What.** Add a new optional field on detour hooks. When set and the handler
returns non-zero, the wrapper jumps to that address instead of resuming
execution at `hookAddress + originalBytes.size()`. This lets a hook
*selectively consume* events at runtime — pass through the engine's normal
flow when the handler returns 0, redirect to e.g. a function epilogue when
it returns 1.

**Why.** The current wrapper at `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp`
unconditionally executes original bytes + JMPs to a fixed return address.
There's no way for a C++ handler to redirect engine flow based on runtime
state. Workarounds (clobbering registers to feed garbage into downstream
switches; in-place opcode patching outside the framework; deduplication of
side-effect announcements) are either fragile, lose framework benefits, or
require muting engine state changes the user can perceive. A clean conditional
flow primitive eliminates this whole class of workarounds.

**Concrete use case (ours).** Our accessibility mod intercepts arrow keys at
`CSWGuiManager::HandleInputEvent` to drive a synthesized cursor across menu
controls. We want the engine to never see the arrow key — no broken `.gui`
focus-cycle handler firing, no parallel `SetActiveControl` events. With this
feature: handler returns 1, wrapper jumps to function epilogue at `0x0040cbcb`,
engine exits as if it had handled the key. For non-arrow keys the handler
returns 0 and normal flow resumes. See `docs/menu-nav-design.md` for the full
context.

**Files to change** (data flow source → wrapper):

- `src/KPatchCore/Models/Hook.cs` — add `public uint? ConsumedExitAddress { get; init; }`
- `src/KPatchCore/Parsers/HooksParser.cs` — parse `consumed_exit_address` from source TOML
- `src/KPatchCore/Applicators/ConfigGenerator.cs` — write `consumed_exit_address` to runtime TOML when non-null
- `src/KotorPatcher/include/patcher.h` — add field to `Patch` struct
- `src/KotorPatcher/src/config_reader.cpp` — parse `consumed_exit_address` from runtime TOML
- `src/KotorPatcher/src/patcher.cpp` — copy `Patch.consumedExitAddress` → `WrapperConfig.consumedExitAddress`
- `src/KotorPatcher/include/wrappers/wrapper_base.h` — add `DWORD consumedExitAddress = 0;` to `WrapperConfig`
- `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp` — emit conditional jump

Total: 8 files, ~50 lines, fully additive. Default behavior unchanged for
every existing hook.

**Wrapper assembly addition.** The cut bytes must be emitted *before* the
conditional jump so the consumed and fall-through paths leave the stack in
the same state — equivalent to the cut having executed natively in-place.
The caller's `consumed_exit_address` is necessarily a point downstream of
the cut, so it expects the stack mutations the cut would have applied.

Restructured tail (replaces the existing skip/run-original split):

```asm
; (register restore complete; eax excluded from POPAD if consuming)
<cut bytes>                            ; emitted unless skipOriginalBytes
TEST EAX, EAX                          ; 85 c0
JZ +5                                  ; 74 05  (skip the consumed JMP)
JMP rel32 to consumed_exit_address    ; e9 ?? ?? ?? ??  (consumed)
JMP rel32 to hookAddress + cut.size() ; fall-through (non-consumed)
```

About 10 emitted bytes for the conditional. Equivalent C++:

```cpp
// 1. Emit cut bytes (existing behavior, gated by skipOriginalBytes)
if (!config.skipOriginalBytes) {
    EmitBytes(code, config.originalBytes.data(), config.originalBytes.size());
}

// 2. Optional consumed-exit conditional
if (config.consumedExitAddress != 0) {
    EmitByte(code, 0x85); EmitByte(code, 0xC0);            // TEST EAX, EAX
    EmitByte(code, 0x74); EmitByte(code, 0x05);            // JZ +5
    EmitByte(code, 0xE9);                                   // JMP rel32
    DWORD off = CalculateRelativeOffset(
        code - 1,
        reinterpret_cast<void*>(config.consumedExitAddress));
    EmitDword(code, off);
}

// 3. Fall-through JMP back to hookAddress + originalBytes.size() (existing)
```

**Caller contract.** Hook author must:
- Set `consumed_exit_address` to a valid resume point inside the hooked function.
- Add `"eax"` to `exclude_from_restore` so the handler's return value survives the wrapper.
- Have the handler return a non-zero `int` to consume.
- Verify the stack state at `consumed_exit_address` matches the stack state at
  `hookAddress + originalBytes.size()` (i.e., the natural fall-through point).
  This is straightforward when the consumed address is the function epilogue,
  because the cut bytes typically only push registers / store struct fields
  and don't change net stack delta.

**Risks.** Very low. Additive feature, default disabled, doesn't change any
existing code path. Caller is responsible for picking a valid jump target —
same kind of address-correctness invariant they already hold for `address`,
`original_bytes`, and any patched DWORDs.

**Open questions.** Whether to add a sanity check in `WrapperConfig` (e.g.,
warn if `consumed_exit_address` is non-zero but `excludeFromRestore` doesn't
include `"eax"`). Probably worth doing as a debug-build assertion.

---

### PR-3. Wrapper selective-POPAD writes ESP via `POP ESP`

**Repo:** `LaneDibello/Kotor-Patch-Manager`
**Status:** Bug confirmed via in-game freeze (session 6, 2026-04-30).
**Discovered:** Session 6 while shipping PR-1 — first time any of our hooks
used a non-empty `exclude_from_restore`, which switches the wrapper from the
hardware `POPAD` opcode to a manual register-pop loop.

**What.** `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp` ~lines 180–193
emits `POP ESP` (opcode 0x5C) for the ESP slot when ESP is not in
`exclude_from_restore`. Hardware `POPAD` *skips* the saved-ESP slot — it
advances ESP by 4 without writing — so the manual loop should always emit
`ADD ESP, 4` for the ESP slot regardless of the exclude list.

**Why this matters.** With `POP ESP`, ESP becomes the saved-PUSHAD value
(the ESP at hook entry). On the very next `POP EBX`, the wrapper reads
from 16 bytes past the wrapper's saved area — random stack memory above
the saved frame — and hands garbage values to EBX/EDX/ECX. From there the
cut bytes corrupt memory (e.g. `MOV [ESI+0x68], EBX` writes via a garbage
ESI for our `HandleInputEvent` cut), then the natural fall-through JMP
returns to the engine with a wrecked register file. In our case the game
froze on the first arrow-key consumption and crashed shortly after.

**Why undetected.** No shipping upstream patch declares a non-empty
`exclude_from_restore`. The selective path is dead code in production until
someone needs to surface a return value (PR-1's exact use case). Once
PR-1 lands and consumed-event hooks become idiomatic, every consumer hits
this bug.

**Fix.** One condition in the loop:

```cpp
constexpr int kEspSlot = 3;
for (int i = 0; i < 8; i++) {
    if (i != kEspSlot && config.ShouldRestoreRegister(regOrder[i])) {
        EmitByte(code, popOpcodes[i]);     // POP reg
    } else {
        EmitByte(code, 0x83); EmitByte(code, 0xC4); EmitByte(code, 0x04);  // ADD ESP, 4
    }
}
```

**Risks.** None — matches the hardware `POPAD` opcode the simple path uses.

**Workaround we currently use.** Fixed locally; we ship the patched
`KotorPatcher.dll` in our `C:\Tools\KotorPatchManager-v0.4.2` runtime.

---

### PR-2. Wrapper LEA-vs-MOV bug for `esp+X` parameter sources

**Repo:** `LaneDibello/Kotor-Patch-Manager`
**Status:** Bug confirmed, not yet filed. We work around it locally.
**Discovered:** Session 3 (2026-04-29) — see `memory/project_kpatchmanager_lea_bug.md`.

**What.** The wrapper at `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp`
lines ~340–361 emits `LEA ECX, [ESP + offset]; PUSH ECX` for any parameter
declared with `source = "esp+X"`. `LEA` computes the address; it does not
dereference. The handler receives the address of the original arg in stack
memory, not the arg's value.

**Why.** Discovered when a hook on `CSWGuiMainMenu::HandleInputEvent` at
function entry with `source = "esp+4"` / `"esp+8"` and `type = "int"`
produced consecutive 4-byte-apart stack addresses (`1768404`, `1768408`)
instead of `InputIndices` enum values. Verified against wrapper source.
Git blame on the "Fix stack offset wrapper" commit (commit `ced6249`,
Jan 2026) only changed comments to describe the LEA behavior — it didn't
change the LEA opcode (`0x8D`) to MOV (`0x8B`).

**Why undetected.** Most shipping patches use register sources only. Two
patches using stack params (`PlanetsLimit`, `EnableScriptAurPostString`)
target GoG and may not have been validated against actual runtime values;
`EnableScriptAurPostString` exercises a debug text-placement function whose
wrong-coordinate output is plausibly missable as "weird" rather than "broken."

**Fix.** One-byte change at three call sites: opcode `0x8D` (LEA r32, m) →
`0x8B` (MOV r32, r/m32). The ModR/M and SIB bytes already point to
`[ESP + disp]` and don't need to change. After the change, the handler
receives the value at `[ESP + offset]`, which is what the parameter
`type = "int"` annotation already implies.

**Risks.** The two upstream patches that use stack params would need to be
re-validated. If their handlers were silently working with addresses-as-ints
the fix breaks them; if they happened to dereference, the fix repairs them.
Either way it surfaces existing bugs rather than introducing new ones.

**Workaround we currently use.** Avoid `source = "esp+X"` entirely. Hook
mid-function and use register sources (per
`memory/feedback_hook_design_register_sources.md`).

---

### PR-4. Wrapper `consumed_exit_address` clobbers EFLAGS via `TEST EAX, EAX`

**Repo:** `LaneDibello/Kotor-Patch-Manager`
**Status:** Bug confirmed via in-game suppression failure (Phase 3 lay-off 5,
2026-05-06).
**Discovered:** While hooking `CSWCCreature::PlayFootstep` for stuck-detection.

**What.** When `consumed_exit_address` is set, the wrapper currently emits
`TEST EAX, EAX` after running the relocated cut bytes, then dispatches on
the resulting ZF (`JZ +5; JMP rel32 consumed_exit`). That TEST clobbers
EFLAGS — including any flags the cut bytes set that the engine's downstream
code at `natural_resume` depends on. The fix is to wrap the TEST in
`PUSHFD/POPFD` so cut-flag state survives the consume check.

**Why.** Many natural cut points in compiled C++ end with a flag-setting
instruction (CMP, TEST, ADD, SUB) whose result is consumed by the very next
engine instruction (a Jcc). Hooking such cuts with `consumed_exit_address`
silently breaks the downstream Jcc — the wrapper's TEST overwrites the cut's
ZF/SF/CF/etc. with `(EAX == 0)`. The handler can't observe this; it just
sees the engine taking the wrong branch.

**Concrete failure (ours).** Phase 3 lay-off 5 hooked
`CSWCCreature::PlayFootstep` at `0x0061a30c` to suppress player footsteps
when stuck. Cut bytes: `MOV EDI, [ESI+0x20]; CMP EDI, EBX` (5 bytes). The
engine's downstream `JZ +0x312` at 0x0061a31a tested ZF from the cut's CMP.
After the wrapper's TEST EAX, EAX, ZF = (handler_return == 0) — so verdict=0
caused the engine's JZ to take the early-out unconditionally → no audio EVER
played. 75 player verdict=0 events fired silently before the bug was traced.

**Concrete failure mode 2 (related EAX-clobber).** A second iteration moved
the hook to `0x0061a320` (cut = `MOV EAX, [ESI+0x21c]`). That cut's first
instruction overwrites EAX *before* the wrapper's TEST EAX, EAX runs, so
TEST tests the appearance pointer (always non-null) instead of the handler
return → JMP consumed_exit always taken → 501 player verdict=0 events fired
silently. This is a corollary of the same root issue: the wrapper's
EAX-as-consume-signal protocol is fragile because cut bytes can clobber EAX
without the user realising.

**Files to change:**

- `src/KotorPatcher/src/wrappers/wrapper_x86_win32.cpp` — in the
  `consumedExitAddress != 0` block (~line 230), wrap the TEST + JZ + JMP
  consumed_exit sequence in PUSHFD/POPFD. The fall-through path also needs
  POPFD before its JMP rel32 natural_resume to symmetrically restore.

**Wrapper assembly change:**

```
Before:
  [run cut bytes]                     ; sets some flags
  TEST EAX, EAX                       ; clobbers ZF/SF/...
  JZ +5                               ; skip JMP rel32
  JMP rel32 consumed_exit
  JMP rel32 natural_resume

After:
  [run cut bytes]                     ; sets some flags
  PUSHFD                              ; save cut's flags
  TEST EAX, EAX                       ; clobbers flags (no longer matters)
  JZ +6                               ; skip POPFD (1) + JMP rel32 (5) = 6
  POPFD                               ; restore cut's flags
  JMP rel32 consumed_exit
  POPFD                               ; restore cut's flags (fall-through)
  JMP rel32 natural_resume
```

(The EAX-clobber issue is a separate constraint: the user MUST design cut
bytes that don't write to EAX before the wrapper's TEST runs. A full fix
would also wrap the consume-signal in a different register or use a stack
slot, but that's a larger redesign — the EFLAGS fix alone closes the
PlayFootstep-style failure mode.)

**Workaround we currently use.** When the natural cut would set
flags-the-engine-depends-on AND there's no way to satisfy the EAX-clean
constraint either, hook AT the engine's flag-consuming Jcc with
`skip_original_bytes = true` and emulate the engine's check inside the
handler. The wrapper then emits no cut bytes, dodging both bugs entirely.
See `OnPlayFootstep` in `audio_footstep_suppress.cpp` for the working
pattern.

---

### PR-5. `PatchApplicator.InstallOptions.AllowVersionMismatch`

**Repo:** `LaneDibello/Kotor-Patch-Manager`
**Status:** Designed, applied locally, not yet drafted. Unblocks installer interop
with widescreen and other community exe-modifying mods.
**Discovered:** Session 11 (2026-05-16) while designing the bundled-mods install
flow in our accessibility installer.

**What.** Add `bool AllowVersionMismatch { get; init; } = false;` to
`PatchApplicator.InstallOptions`. When the caller sets it true, a SHA-256
mismatch between the detected `swkotor.exe` and any patch manifest's
`[patch.supported_versions]` dict is demoted from a hard error to a warning
in `InstallResult.Messages`. The install proceeds and relies on the per-hook
`original_bytes` verification at `StaticHookApplicator.cs:74` as the actual
safety net for static hooks.

**Why.** KPatchCore's current `GameVersionValidator.ValidateAllPatchesSupported`
gate at `PatchApplicator.cs:190` short-circuits the install with "unsupported
game version" for any exe whose SHA-256 isn't pre-baked into the manifest.
This is overly strict given that:

- `StaticHookApplicator` already performs a byte-level check at apply time —
  for each static hook it reads `OriginalBytes.Length` bytes at `hook.Address`
  and aborts with a clear mismatch error if they differ. That's the real
  safety net; the SHA check is a friendlier early failure for the common case.
- DLL-only patches (no static hooks) don't actually corrupt anything when run
  on an unexpected exe — at worst the runtime hooks miss their targets and
  the DLL silently no-ops. Degradation, not corruption.
- Any modification of `swkotor.exe` outside the patcher's awareness (UniWS
  widescreen, ndix UR's HR Menus, future Aspyr/Steam updates) trips the gate
  even when our hooks would still apply cleanly because the modifications
  touched unrelated bytes.

**Concrete use case (ours).** Our accessibility installer wants to bundle the
canonical community widescreen mods (UniWS + KOTOR High Resolution Menus, per
the neocities full build's "Essential / 1" tier). Both patch `swkotor.exe`.
Without this PR, installing widescreen first changes the exe hash and our
accessibility `.kpatch` refuses to apply afterwards. Re-implementing widescreen
as a KPatchCore patch would diverge from the community-canonical setup the user
explicitly wants.

**Files to change:**

- `src/KPatchCore/Applicators/PatchApplicator.cs` — `InstallOptions` record gains
  `AllowVersionMismatch` (default false), and the post-validator branch demotes
  the failure to a `Messages.Add("WARNING: ...")` + continue when the flag is
  true.

Total: 1 file, ~15 lines, fully additive. Default behavior unchanged for every
existing caller (`kdev apply`, upstream `cli-kpatch`, etc.).

**Risks.** Low. The opt-in is the caller's affirmative statement that the
modified-exe scenario is expected. For static-hook patches the byte verification
at `StaticHookApplicator.cs:74` is a strict gate and will fail any hook whose
original bytes don't match. For DLL-only patches the worst case is silently
inert hooks at runtime — a regression compared to "early refusal" but not a
corruption risk.

**Open questions.**

- Whether the warning text should also recommend a manifest update path
  (i.e., "add hash X to your supported_versions to suppress this warning").
  Probably yes — surfaces the cleaner long-term fix.
- Whether to add the same flag to `cli-kpatch` as `--allow-version-mismatch`
  so command-line callers have parity. Trivial follow-up.

---

### PR-6. K1CP ships `.lyt` / `.vis` with LF-only line endings, crashes engine parser

**Repo:** `KOTORCommunityPatches/K1_Community_Patch`
**Status:** Designed, not yet drafted. Workaround shipped in our installer
(`installer/KotorAccessibilityInstaller/ModInstallers/K1cpInstaller.cs`,
`NormalizeOverrideLineEndings` runs after HoloPatcher returns).
**Discovered:** Session 2026-05-27 while debugging a deterministic crash
loading the Leviathan-bridge cutscene (`stunt_03a` / `stunt_levbridge`).

**What.** Several `.lyt` and `.vis` files under `tslpatchdata/` are
committed with Unix line endings (LF-only) and at least one
(`stunt_levbridge.lyt`) has no trailing newline at all. The KOTOR 1
layout parser (`CLYT::LoadLayout` @ `0x005de900` in `swkotor.exe` 1.0.3
Steam/GoG) advances its read cursor by `strlen(line) + 2` after every
`sscanf` call, hardcoding the two-byte CRLF assumption. With LF-only
input the cursor over-advances by one byte per line. After N lines the
drift is N bytes; the cursor eventually walks off the end of the heap-
allocated layout buffer (allocated as `operator_new(file_size + 4)` near
the top of the function). `sscanf` then calls strlen internally on the
dangling pointer, which scans forward until it hits an unmapped page
and crashes with `STATUS_ACCESS_VIOLATION`.

K1CP's `.gitattributes` does not normalise these extensions, so they get
committed and shipped with whatever line endings the contributing
editor's setup emitted at commit time.

**Affected files (at K1CP commit `4778ae5e`, the version we pin):**

- `tslpatchdata/stunt_levbridge.lyt` (1032 bytes, 23 LF, **0 CR**, no trailing newline)
- `tslpatchdata/stunt_levbridge.vis` (204 bytes, 16 LF, 0 CR)
- `tslpatchdata/stunt_endbridge.lyt`, `stunt_endbridge.vis`
- `tslpatchdata/stunt_starforge.lyt`, `stunt_starforge.vis`
- `tslpatchdata/stunt_unkramp.lyt`, `stunt_unkramp.vis`
- `tslpatchdata/m40ad.lyt`, `m40ad.vis` (Leviathan main area)
- `tslpatchdata/m02ac.vis`, `m02ae.vis`, `m03ae.vis` (Taris)
- `tslpatchdata/m10aa.vis` (Tatooine)
- `tslpatchdata/m13aa.vis` (Manaan)
- `tslpatchdata/m17aa.vis` (Unknown World)

In total, 4 `.lyt` and 10 `.vis` files in the live install. There are
likely more we didn't survey — the contributing editor's line-ending
choice is non-deterministic across the patch's history. A repo-wide
`file *.lyt *.vis` audit would catch the rest.

**Why this is latent for most installs.** Whether the drift crashes
depends on heap layout. The over-run reads past the end of the layout
buffer; if the trailing bytes happen to live in a still-committed heap
page, strlen returns a wrong-but-finite length and the parser silently
miscomputes downstream room/door positions (cutscene-visual glitch,
silently ignored). If the trailing bytes live in a decommitted page, the
process crashes. Our installer adds Prism + SAPI + dsoal + a 3 MB DLL,
which is enough additional heap pressure to push the decommit boundary
into the over-run region. We see the crash; vanilla K1CP users typically
do not.

**Repro.** With K1CP `4778ae5e` installed via HoloPatcher and any
additional process loading enough memory to perturb the allocator, start
a new game, play through Taris to the rooftop Hidden-Bek encounter, and
confirm the party-selection screen. The Leviathan-capture cutscene load
(`stunt_03a` → `stunt_levbridge`) crashes inside
`CLYT::LoadLayout+0x117` (the `_sscanf` call site), with EIP at
`_strlen+0x30` (`0x00701330`) and the faulting address landing on a
4096-aligned (page-boundary) heap address. The address differs per run;
same engine frame each time. Two dumps from the session:
`swkotor.exe(1).23224.dmp` (faulting addr `0x18316000`) and
`swkotor.exe.31792.dmp` (faulting addr `0x14127000`).

Pure-vanilla repro: remove our DLL. The crash typically does not
reproduce — the allocator stays in a state where the over-run lands on a
recycled page. Pre-allocating ~16 MB of small chunks in any other
injected DLL before the cutscene load is enough to flip many systems
into the crash regime, but the threshold is environment-dependent.

**Fix (proposed).** Two complementary changes to the K1CP repo:

1. Add to `.gitattributes` (alongside the existing
   `/tslpatchdata export-ignore`):
   ```
   *.lyt text eol=crlf
   *.vis text eol=crlf
   ```
   This makes `git checkout` materialise the files with CRLF on every
   platform regardless of the local `core.autocrlf` setting, keeping
   contributors honest going forward.

2. Run `unix2dos *.lyt *.vis` (or equivalent) once over the existing
   `tslpatchdata/` directory and commit the converted files. Pure
   format-only change, no content change, easy to review by diff.

Also worth adding a CI check that fails the build if any `.lyt` / `.vis`
under `tslpatchdata/` lacks CRLF — keeps a future contributor's editor
from silently regressing it.

**Our workaround** lives in
`installer/KotorAccessibilityInstaller/ModInstallers/K1cpInstaller.cs`:
after HoloPatcher returns success, `NormalizeOverrideLineEndings` walks
the game's Override directory and converts any LF-only `.lyt` / `.vis`
file to CRLF in-place, appending a trailing CRLF when the source file
lacks any line terminator at all. Idempotent — files already containing
CR are skipped, and empty files / no-LF files are skipped too.

**Why this should ship as an upstream fix, not stay as a downstream
workaround.** Any K1CP user without our memory pressure currently has
*silent* parsing miscomputes — every layout / visibility table parsed
slightly wrong, with the cumulative drift growing with the file's line
count. They don't crash, but they're playing with broken room positions
and broken visibility tables. The visible effect is subtle (rooms
slightly mis-positioned in the layout grid; visibility errors that may
manifest as rooms not lighting properly or geometry popping in/out).
Fixing line endings is essentially free and benefits every K1CP user.
Crash protection is only the most visible symptom of the bug.

**Files to change (upstream):**

- `.gitattributes` — add 2 lines for `.lyt` / `.vis` CRLF enforcement
- `tslpatchdata/*.{lyt,vis}` — re-commit all of them with CRLF (one
  cleanup commit; pure line-ending change)
- Optional: a CI check (`.github/workflows/check-eol.yml`) preventing
  regression

**Risks.** None. Line endings are invisible to the engine's parser other
than the +2 advance; converting LF → CRLF makes existing files parse
correctly without changing any content the engine cares about. Our
workaround verified in-game 2026-05-27 — the `stunt_03a` cutscene that
crashed deterministically before the fix loads cleanly after, with the
K1CP content (the added `M40ad_777` room, improved Saul Karath model,
etc.) intact.

**Open questions.**

- Whether to also normalise other text formats K1CP commits to
  `tslpatchdata/` (e.g. `.nss` script sources, `.txt` docs, `.ini`
  config). Likely fine but out of scope for this fix; the engine doesn't
  parse those at runtime in the same way. K1CP's TSL-Patcher toolchain
  may or may not assume CRLF for its own consumption — confirm before
  broadening.
- Whether KOTOR 2 / TSLRCM has the same engine quirk. The TSL engine
  shares most of the layout parser with K1 (Aurora → Odyssey port), so
  likely yes; TSLRCM may have the same latent issue. Out of scope for
  this PR; worth mentioning to the K1CP maintainers as a hint to check
  the sibling project.

---

### PR-7. prism `acquire_best`/`create_best` crash when a backend's `initialize()` faults

**Repo:** `ethindp/prism`
**Status:** Bug confirmed (pl-PL beta tester, ZDSR). Fixed locally atop upstream
v0.16.5; recorded in `docs/prism-local-patches.patch`. Not yet drafted as a PR.
**Discovered:** v0.2.1 startup crash; re-confirmed against upstream v0.16.5
(2026-06-13) — the unguarded loop is unchanged in the latest release.

**What.** `BackendRegistry::acquire_best()` and `create_best()`
(`source/backends/backend_registry.cpp`) call each candidate backend's
`initialize()` in priority order with no exception handling. On Windows, the
ZDSR / PC-Talker / BoyPC backends reach their reader by delay-loading a vendor
DLL (`ZDSRAPI.dll`, `PCTKUSR.dll`, `BoyCtrl.dll`, marked `/delayload` in
`CMakeLists.txt`). If the user's installed DLL exports a mismatched symbol set,
the MSVC delay-load helper raises a structured exception (`0xC06D007F`
PROC_NOT_FOUND / `0xC06D007E` MOD_NOT_FOUND) from inside `initialize()`. Being a
structured exception (not a C++ exception), it isn't caught by `/EHsc` and
propagates straight out of `acquire_best`, crashing the host process before any
speech.

**Why undetected.** NVDA/JAWS/SAPI win priority and `acquire_best` returns
before reaching a broken low-priority backend. Only a user whose higher-priority
readers all fail (e.g. ZDSR-only) walks into the faulting backend. NVDA reaches
its reader via a compiled-in RPC stub, JAWS/SAPI/OneCore via COM/OS APIs — none
delay-load a vendor DLL, so they never trip it.

**Fix.** Wrap each backend's `initialize()` in SEH on Windows and treat a fault
as "failed to initialize" — skip the backend and continue the priority walk
(down to SAPI as the universal catch-all) instead of crashing. A faulting
backend should never take down backend selection for every other reader. See
`docs/prism-local-patches.patch` for the exact diff (a `seh_safe_initialize`
helper holding only PODs to satisfy MSVC C2712, plus a portable `try_initialize`
wrapper used by both `acquire_best` and `create_best`).

**Risks.** Low. Behaviour is unchanged on the common path (no fault). On a
faulting backend the only change is "skip instead of crash." `BackendResult<>`
is `std::expected<void, BackendError>` with a trivially destructible payload, so
the SEH helper compiles clean (no unwinding required). Non-Windows builds use a
plain call — no SEH.

**Note.** This makes selection crash-safe; it does NOT make a mismatched-DLL
ZDSR install actually speak through ZDSR (it cleanly falls to SAPI). Making ZDSR
bind is a separate fix — upstream already did the analogous thing for
SystemAccess in v0.16.x ("Rewrote the SystemAccess backend to no longer require
the delay-loaded DLL"); the same de-delay-load treatment would fix ZDSR.

---

## Conventions

- One PR per coherent change. Keep them small and reviewable.
- Each entry above is a self-contained brief; the actual PR description copies
  the relevant sections.
- When a PR ships, move the entry to a "Shipped" section below with date and
  link.
- When upstream gets fixed, audit our local workarounds and remove them.

## Shipped

(none yet)
