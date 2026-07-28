# save_crash_guard.h (25 lines)

Declares the fix for the engine's save-game divide-by-zero crash: with
"Frame Buffer Effects" off (swkotor.ini Frame Buffer=0), `AurSaveGameSnapshot`'s
glReadPixels capture comes back 0-sized and `ImageScale` divides by
`srcW*srcH` unguarded, raising 0xc0000094. See memory
project_save_crash_imagescale_framebuffer.

## Declarations (in source order)

- L23 — `void InstallSaveScreenshotGuard()`
  note: idempotent; call once after engine init (OnRulesInit); no-op + logs on any install failure
