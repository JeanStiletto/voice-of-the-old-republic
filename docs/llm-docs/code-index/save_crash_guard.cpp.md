# save_crash_guard.cpp (152 lines)

Installs an inline trampoline detour over the engine's `ImageScale`
(`@0x0045dad0`, addr::R-rebased) so a degenerate source area (srcW/srcH <= 0)
returns a zeroed destination buffer instead of dividing by zero. Builds a
hand-assembled executable block via VirtualAlloc: relocated 8-byte prologue +
JMP back to entry+8 (the trampoline, callable as "the original"), plus a
JMP-indirect wrapper stub pointing at `ImageScaleGuard`, then patches
ImageScale's entry with a JMP rel32 into the stub. Allocates the replacement
buffer with the engine's own `operator new` (`@0x006fa7e6`) so the caller's
matching `_free` releases it correctly. Talks to engine_rebase.h (addr::R) and
log.h.

## Declarations (in source order)

- L17 — `const uintptr_t kImageScaleAddr = acc::addr::R(0x0045dad0)`
  note: void* __cdecl ImageScale(byte* src, int srcW, int srcH, int bpp, int dstW, int dstH); divisor srcW*srcH unguarded
- L20 — `const uintptr_t kImageScaleContinue = acc::addr::R(0x0045dad8)`
  note: entry+8, just past the 3 relocated prologue instructions
- L24 — `const uintptr_t kEngineOperatorNew = acc::addr::R(0x006fa7e6)`
- L28 — `constexpr uint8_t kPrologue[8]`
  note: SUB ESP,0x30 / PUSH EBX / MOV EBX,[ESP+0x4c] — none position-relative, relocate verbatim
- L34 — `typedef void* (__cdecl* PFN_ImageScale)(...)`
- L36 — `typedef void* (__cdecl* PFN_OperatorNew)(size_t)`
- L40 — `PFN_ImageScale g_origImageScale`
  note: trampoline pointer; calling it == calling unhooked ImageScale
- L42 — `PFN_OperatorNew g_engineNew`
- L47 — `void* __cdecl ImageScaleGuard(unsigned char* src, int srcW, int srcH, int bpp, int dstW, int dstH)`
  note: passthrough when srcW/srcH>0; else synthesizes a zeroed dst buffer capped at 64MB, else passes through
- L81 — `void WriteRel32Jmp(uint8_t* at, uintptr_t target)`
- L90 — `void InstallSaveScreenshotGuard()`
  note: builds 23-byte exec block (trampoline+stub+absolute slot), VirtualProtect-patches entry, FlushInstructionCache both sites
