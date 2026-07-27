# engine_subscreen.h (99 lines)

Public contract for engine_subscreen.cpp: sub-screen redrill cleanup,
in-world overlay-pause holds (owner-bitmask, stacking-safe), and the three
detour entry points (OnSwitchToSWInGameGui, OnHideSWInGameGui,
OnSetSWGuiStatus).

## Declarations (in source order)

- L16 — `namespace acc::engine`
- L20 — `extern bool g_switchHookEverFired`
- L34 — `void TickInputClassReassert()`
- L53 — `enum class OverlayPauseOwner : unsigned { UnifiedMenu, CombatQueue, ExamineView, Help, LevelUp }`
- L60 — `void BeginOverlayPause(OverlayPauseOwner owner)`
- L61 — `void EndOverlayPause(OverlayPauseOwner owner)`
- L69 — `bool WorldIsPaused()`
- L78 — `bool ResumeWorldIfPaused(const char* reason)`
- L84 — `extern "C" void OnSwitchToSWInGameGui(void*, int guiId)`
  note: detour @0x0062cf2d, 5-byte cut after EBX load.
- L90 — `extern "C" void OnHideSWInGameGui(void*, void*)`
  note: detour @0x0062cba0, 9-byte cut, all-relative operands.
- L97 — `extern "C" void OnSetSWGuiStatus(void*, void*, void*)`
  note: detour @0x0062aa00, 5-byte cut; status 1=in-world,2=main menu,3=sub-screen.
