# Upstream PR Opportunities

Tracks fixes and features we'd offer back to upstream projects. Each entry
captures: what, why, where, and current state.

Most upstream is `LaneDibello/Kotor-Patch-Manager`. Others added as they come up.

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

## Conventions

- One PR per coherent change. Keep them small and reviewable.
- Each entry above is a self-contained brief; the actual PR description copies
  the relevant sections.
- When a PR ships, move the entry to a "Shipped" section below with date and
  link.
- When upstream gets fixed, audit our local workarounds and remove them.

## Shipped

(none yet)
