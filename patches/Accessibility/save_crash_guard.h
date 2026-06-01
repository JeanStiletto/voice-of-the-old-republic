// Save-game crash guard (engine ImageScale divide-by-zero).
//
// CServerExoAppInternal::SaveGame -> CAppManager::DoSaveGameScreenShot ->
// AurSaveGameSnapshot -> ImageScale. AurSaveGameSnapshot captures the
// framebuffer via glReadPixels and hands ImageScale the source dimensions.
// With "Frame Buffer Effects" disabled (swkotor.ini Frame Buffer=0) — the
// documented KOTOR save-crash trigger — that capture comes back 0-sized, and
// ImageScale divides by (srcW*srcH) without guarding it (it only guards the
// destination area), so saving #DE's (0xc0000094 INTEGER_DIVIDE_BY_ZERO).
//
// We install an inline trampoline detour over ImageScale: when the source
// area is degenerate we return a zeroed destination buffer (the save just
// gets a blank thumbnail — irrelevant for our audience) instead of dividing;
// otherwise the original runs untouched. See memory
// project_save_crash_imagescale_framebuffer.

#pragma once

namespace acc::save_guard {

// Idempotent. Call once, after the engine is up (from OnRulesInit, same as
// the DirectInput mouse guard). No-op on any install failure (logged).
void InstallSaveScreenshotGuard();

}  // namespace acc::save_guard
