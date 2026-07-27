# engine_options.h (51 lines)

CClientOptions helpers — Mouse Look toggle and the Action Menu auto-pause
setting. Documents the chain AppManager → +0x4 CClientExoApp → +0x4
Internal → +0x4 CClientOptions* → +0x8 int bitfield (5 bits: auto_level,
mouse_look, autosave, minigame_yaxis, combat_movement) plus a SEPARATE
bit_flags_2 (+0x14) that carries the AutoPause options.

## Declarations (in source order)

- L19 — `namespace acc::engine`
- L22 — `bool GetMouseLook(bool& out)`
- L31 — `bool GetActionMenuAutoPause(bool& out)`
  note: bit_flags_2 (+0x14) bit 0xf (mask 0x8000) — a DIFFERENT bitfield from mouse_look's +0x8; mirrors CSWGuiMainInterface::OnTargetUpArrowPressed/OnActionUpArrowPressed's own gate; off by default.
- L35 — `void* GetClientOptions()`
  note: exposed for diagnostic probes; production code should use Get/Set/ToggleMouseLook.
- L37 — `bool SetMouseLook(bool enabled)`
- L40 — `bool ToggleMouseLook(bool& outNew)`
  note: read-modify-write; false on either failure; outNew = new value on success.
- L44 — `constexpr unsigned int kClientAppOptionsOffset      = 0x4`
- L45 — `constexpr unsigned int kClientOptionsBitFieldOffset = 0x8`
- L46 — `constexpr unsigned int kClientOptionsMouseLookMask  = 0x2`
- L50 — `constexpr unsigned int kClientOptionsAutoPauseFlagsOffset    = 0x14`
- L51 — `constexpr unsigned int kClientOptionsActionMenuAutoPauseMask = 0x8000`
