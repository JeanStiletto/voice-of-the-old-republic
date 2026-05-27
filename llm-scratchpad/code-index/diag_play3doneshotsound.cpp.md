# diag_play3doneshotsound.cpp (57 lines)

Implementation of the Play3DOneShotSound diagnostic. Single C-linkage
hook handler registered via hooks.toml (currently commented out —
`# function = "OnPlay3DOneShotSound"` in hooks.toml line 312).

The hook reads the CResRef* from [esp+4] and the return EIP from [esp+0]
(framework emits LEA not MOV for esp+X params, so arg_addr is the address
of the slot, not its value). Logs one line per fire as
`Play3DOneShot: caller=0x%08x resref=[%s]`.

## Declarations (in source order)

- L32 — `extern "C" void __cdecl OnPlay3DOneShotSound(void* arg_addr)`
  note: hook at CExoSound::Play3DOneShotSound; currently disabled in hooks.toml (commented out); reads resref + caller EIP and logs; no side effects
