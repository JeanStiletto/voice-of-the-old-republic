---
name: Tab-crash dump analysis findings
description: What kdev analyze-dump surfaced about the mgr+5 crash signature — EBP corruption to mgr ptr, 0x202 on stack, MainLoop in chain. Informs Phase 3 click-sim design.
type: project
originSessionId: b2d8fb3d-a452-493b-8007-4ad2e397d44d
---
Tab-crash dumps captured in %LOCALAPPDATA%\CrashDumps from May 2 sessions, analyzed via kdev analyze-dump. Two are identical Tab-path crashes; one is a different shutdown crash.

**The Tab-path signature (`24928.dmp`, `27200.dmp`):**
- Exception: 0xC0000005 ACCESS_VIOLATION write to 0x00000000.
- EIP=mgr+5 (manager pointer + 5; mgr is heap-allocated so the absolute address differs run to run).
- **EBP=mgr** at fault time. The bad indirect CALL/JMP corrupted EBP to the manager pointer alongside EIP. Why the EBP chain dies one frame in.
- ESP=0x001afd14 (consistent across runs).
- ESP scan resolves to MainLoop+0xdf0, FrameHandler_007273a8 (VC++ SEH entry), inline_unlock_8+0x7, unlock_8+0x5 — same layout in both dumps.
- Stack hex at ESP starts `02 02 00 00 ...` — engine-internal event code 514 sitting at top of stack as a function arg. Same 514 the manager logs as param_2 on press edges (per project_engine_event_value_layers.md, NOT WM_LBUTTONUP — engine-internal, not a Win32 message). Confirms the engine was mid-event-dispatch when the bad indirect call fired.
- Top of stack also contains the mgr pointer twice more around ESP+0x18 and ESP+0x20.

**Why:** The engine's normal click pipeline maintains an invariant that SetActiveControl skips. Some pointer (likely a vtable slot or callback stored relative to the manager) gets written via the natural HandleLMouseDown→HandleLMouseUp flow but not via SetActiveControl alone. When the next code path does an indirect CALL through that pointer, it lands on the manager struct (data, not code), simultaneously corrupting EBP to the same value.

**How to apply:** Phase 3 click-sim primitive must invoke the engine's HandleLMouseDown + HandleLMouseUp directly (rather than SetActiveControl) so the invariant is satisfied. The 0x202 / WM_LBUTTONUP value on the stack is consistent with the engine being mid-mouse-up dispatch when the bad call fires — the click-sim must also leave that flow intact. **Status (2026-05-02):** click-sim is live and the design hewed to this guidance; tab-cycle and Enter-on-button no longer crash. Memory retained as historical record of why we use click-sim instead of SetActiveControl.

**Tool:** `kdev analyze-dump [path]` — defaults to newest dump in %LOCALAPPDATA%\CrashDumps. Reads the saved CONTEXT from the minidump's exception stream (not the live post-unwind context, which is useless for stack walking back into engine code). Resolves all addresses via Lane's Ghidra XML at docs/llm-docs/re/k1_win_gog_swkotor.exe.xml.
